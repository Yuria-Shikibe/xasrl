#pragma once

#include <algorithm>
#include <concepts>
#include <limits>
#include <meta>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "model.hpp"
#include "../codec/reflected.hpp"
#include "../container.hpp"

namespace srl::detail {
    consteval std::size_t annotation_count(std::meta::info entity, std::meta::info annotation) {
        entity = recursively_dealias(entity);
        return std::meta::annotations_of_with_type(entity, annotation).size();
    }

    consteval std::vector<std::meta::info> schema_entities(std::meta::info type) {
        auto result = std::meta::bases_of(type, std::meta::access_context::unchecked());
        for (auto member: std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) {
            result.push_back(member);
        }
        return result;
    }

    consteval bool schema_has_fields(std::meta::info type) {
        type = recursively_dealias(type);
        if (!std::meta::is_class_type(type)) {
            return false;
        }
        if (annotation_count(type, ^^tag::auto_seq) != 0) {
            return true;
        }
        for (auto entity: schema_entities(type)) {
            if (annotation_count(entity, ^^tag::field) != 0) {
                return true;
            }
        }
        return false;
    }

    consteval bool schema_contains_pointer(std::meta::info type) {
        if (std::meta::is_reference_type(type) || std::meta::is_pointer_type(type)
            || std::meta::is_member_pointer_type(type)) {
            return true;
        }
        type = std::meta::remove_cv(type);
        if (std::meta::is_array_type(type)) {
            return schema_contains_pointer(std::meta::remove_extent(type));
        }
        if (std::meta::is_class_type(type) || std::meta::is_union_type(type)) {
            for (auto entity: schema_entities(type)) {
                if (schema_contains_pointer(std::meta::type_of(entity))) {
                    return true;
                }
            }
        }
        return false;
    }

    consteval bool schema_forced_atomic(std::meta::info owner, std::meta::info entity) {
        const auto declared = std::meta::type_of(entity);
        const auto type = std::meta::remove_cvref(declared);
        const bool bypass = annotation_count(owner, ^^tag::bypass_pointer_check) == 1
                            || annotation_count(entity, ^^tag::bypass_pointer_check) == 1;
        const auto atoms = std::meta::annotations_of_with_type(entity, ^^tag::atomic);
        if (std::meta::is_reference_type(declared) || std::meta::is_bit_field(entity)
            || annotation_count(entity, ^^conversion_tag) != 0
            || annotation_count(entity, ^^tag::container) != 0
            || (atoms.size() != 1 && !(bypass && schema_contains_pointer(type)))) {
            return false;
        }
        const auto options = atoms.size() == 1 ? std::meta::extract<tag::atomic>(atoms.front()) : tag::atomic{};
        return std::meta::is_trivially_copyable_type(type)
               && (options.allow_padding || std::meta::has_unique_object_representations(type))
               && (bypass || !schema_contains_pointer(type));
    }

    // These bridges instantiate user-provided C++ customization interfaces only.
    // Tree traversal, annotation processing and size arithmetic remain data operations.
    struct schema_type_facts {
        bool atomic{};
        bool container{};
        std::size_t extent = std::dynamic_extent;
        std::size_t count = std::dynamic_extent;
        std::uint8_t group{};
        //TODO support by vector?
        std::meta::info first{};
        std::meta::info second{};
        atomic_representation representation = atomic_representation::codec;
    };

    template<typename T>
    inline constexpr schema_type_facts schema_type_facts_v = [] consteval {
        schema_type_facts result;
        if constexpr (atomic_serializable<T>) {
            result.atomic = true;
            result.extent = atomic_codec<T>::fixed_size;
            if constexpr (requires { atomic_codec<T>::uses_direct_object_representation; }) {
                if (atomic_codec<T>::uses_direct_object_representation) {
                    result.representation = atomic_representation::direct;
                }
            }
            if constexpr (requires { atomic_codec<T>::uses_segmented_object_representation; }) {
                if (atomic_codec<T>::uses_segmented_object_representation) {
                    result.representation = atomic_representation::segmented;
                }
            }
        } else if constexpr (adapted_container<T>) {
            using traits = container_traits<T>;
            result.container = true;
            result.count = traits::fixed_size;
            result.group = traits::mapping_group_size;
            if constexpr (traits::mapping_group_size == 1) {
                result.first = ^^typename traits::element_type;
            } else {
                result.first = ^^typename traits::key_type;
                result.second = ^^typename traits::mapped_type;
            }
        }
        return result;
    }();

    consteval schema_type_facts schema_facts(std::meta::info type) {
        return std::meta::extract<schema_type_facts>(
            std::meta::substitute(^^schema_type_facts_v, {type}));
    }

    template<typename Memory, typename Storage>
    inline constexpr bool schema_conversion_valid_v =
        std::default_initializable<Storage> && std::move_constructible<Storage>
        && std::is_move_assignable_v<Memory>;

    template<typename Memory, typename Storage>
    inline constexpr bool schema_cast_valid_v = requires(const Memory &mem, Storage &&storage, Memory &out) {
        static_cast<Storage>(mem);
        out = static_cast<Memory>(std::move(storage));
    };

    consteval bool schema_default_initializable(std::meta::info type) {
        return std::meta::extract<bool>(std::meta::substitute(^^std::default_initializable, {type}));
    }

    struct schema_builder {
        bool valid = true;
        std::vector<schema_node> nodes;
        std::vector<std::size_t> edges;
        std::vector<std::meta::info> ancestors;

        consteval std::size_t add_size(std::size_t left, std::size_t right) {
            if (left == std::dynamic_extent || right == std::dynamic_extent) {
                return std::dynamic_extent;
            }
            constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
            if (left > maximum || right > maximum - left) {
                valid = false;
                return std::dynamic_extent;
            }
            return left + right;
        }

        consteval std::size_t multiply_size(std::size_t count, std::size_t size) {
            if (count == 0 || size == 0) {
                return 0;
            }
            if (count == std::dynamic_extent || size == std::dynamic_extent) {
                return std::dynamic_extent;
            }
            if (count > std::numeric_limits<std::uint32_t>::max() / size) {
                valid = false;
                return std::dynamic_extent;
            }
            return count * size;
        }

        consteval bool atomic_metadata_valid(std::meta::info type) {
            if (annotation_count(type, ^^tag::auto_seq) != 0
                || annotation_count(type, ^^tag::auto_stream) != 0
                || annotation_count(type, ^^tag::bypass_pointer_check) > 1) {
                return false;
            }
            for (auto entity: schema_entities(type)) {
                const auto transient = annotation_count(entity, ^^tag::transient);
                const auto bypass = annotation_count(entity, ^^tag::bypass_pointer_check);
                if (annotation_count(entity, ^^tag::auto_stream) != 0 || transient > 1 || bypass > 1
                    || annotation_count(entity, ^^tag::field) != 0
                    || annotation_count(entity, ^^tag::atomic) != 0
                    || annotation_count(entity, ^^tag::container) != 0
                    || annotation_count(entity, ^^conversion_tag) != 0) {
                    return false;
                }
                if (transient == 1) {
                    if (std::meta::is_base(entity) || bypass != 0) {
                        return false;
                    }
                    continue;
                }
                if ((std::meta::is_base(entity) && std::meta::is_virtual(entity))
                    || std::meta::is_bit_field(entity)
                    || std::meta::is_reference_type(std::meta::type_of(entity))
                    || (schema_contains_pointer(std::meta::type_of(entity))
                        && annotation_count(type, ^^tag::bypass_pointer_check) == 0 && bypass == 0)) {
                    return false;
                }
            }
            return true;
        }

        consteval std::size_t append(schema_node node, bool stream = false) {
            const auto index = nodes.size();
            nodes.push_back(node);
            const auto type = node.type;
            if (node.representation == atomic_representation::forced) {
                node.kind = schema_kind::atomic;
                node.encode_extent = node.decode_extent = std::meta::size_of(type);
                nodes[index] = node;
                return index;
            }
            if (std::ranges::find(ancestors, type) != ancestors.end()) {
                valid = false;
                return index;
            }
            const auto seq_count = annotation_count(type, ^^tag::auto_seq);
            const auto stream_count = annotation_count(type, ^^tag::auto_stream);
            const auto atom_count = annotation_count(type, ^^tag::atomic);
            stream = stream || stream_count != 0;
            const auto facts = schema_facts(type);
            if (stream_count > 1 || (stream_count != 0 && seq_count != 0)
                || (stream && (!std::meta::is_class_type(type) || facts.atomic || facts.container || atom_count != 0))
                || atom_count > 1) {
                valid = false;
                return index;
            }
            node.type_hash = srl::type_hash(type);
            if (atom_count != 0 && (!facts.atomic || !atomic_metadata_valid(type))) {
                valid = false;
                return index;
            }
            if (facts.atomic) {
                node.kind = schema_kind::atomic;
                node.encode_extent = node.decode_extent = facts.extent;
                node.representation = facts.representation;
                if (facts.extent > std::numeric_limits<std::uint32_t>::max()) {
                    valid = false;
                }
                nodes[index] = node;
                return index;
            }

            ancestors.push_back(type);
            std::vector<std::size_t> children;
            node.header_size = entry_head_wire_size;
            if (facts.container) {
                node.kind = schema_kind::container;
                node.wire_kind = entry_kind::container;
                node.mapping = mapping_kind::element;
                node.mapping_group_size = facts.group;
                node.element_count = facts.count;
                children.push_back(append(schema_node{
                    .role = facts.group == 1 ? schema_role::element : schema_role::key,
                    .type = std::meta::remove_cvref(facts.first), .memory_type = facts.first,
                }));
                if ((facts.count == std::dynamic_extent || facts.group == 2)
                    && !schema_default_initializable(facts.first)) {
                    valid = false;
                }
                if (facts.group == 2) {
                    children.push_back(append(schema_node{
                        .role = schema_role::mapped,
                        .type = std::meta::remove_cvref(facts.second), .memory_type = facts.second,
                    }));
                    if (!schema_default_initializable(facts.second)) {
                        valid = false;
                    }
                }
                std::size_t stride = facts.group * entry_mapping_wire_size;
                for (auto child: children) {
                    stride = add_size(stride, nodes[child].encode_extent);
                }
                node.encode_extent = add_size(entry_head_wire_size, multiply_size(facts.count, stride));
            } else if ((!stream && !schema_has_fields(type)) || seq_count > 1
                       || annotation_count(type, ^^tag::bypass_pointer_check) > 1
                       || (annotation_count(type, ^^tag::bypass_pointer_check) != 0 && !stream && seq_count != 1)) {
                valid = false;
            } else {
                node.kind = stream ? schema_kind::stream : schema_kind::record;
                node.wire_kind = stream ? entry_kind::stream : entry_kind::record;
                node.mapping = stream ? mapping_kind::none : mapping_kind::field;
                node.mapping_group_size = stream ? 0 : 1;
                std::uint64_t next_id{};
                for (auto entity: schema_entities(type)) {
                    auto child = field(type, entity, stream, seq_count == 1, next_id);
                    if (child) {
                        children.push_back(append(*child, annotation_count(entity, ^^tag::auto_stream) == 1));
                    }
                }
                if (!stream) {
                    std::ranges::sort(children, [&](auto left, auto right) { return nodes[left].id < nodes[right].id; });
                    for (std::size_t i = 1; i < children.size(); ++i) {
                        if (nodes[children[i - 1]].id == nodes[children[i]].id) {
                            valid = false;
                        }
                    }
                }
                node.encode_extent = add_size(entry_head_wire_size,
                    stream ? 0 : multiply_size(children.size(), entry_mapping_wire_size));
                for (auto child: children) {
                    node.encode_extent = add_size(node.encode_extent, nodes[child].encode_extent);
                }
            }
            ancestors.pop_back();
            node.children_begin = edges.size();
            node.children_count = children.size();
            edges.insert(edges.end(), children.begin(), children.end());
            nodes[index] = node;
            return index;
        }

        consteval std::optional<schema_node> field(std::meta::info owner, std::meta::info entity,
                                                   bool stream, bool sequence, std::uint64_t &next_id) {
            const auto fields = std::meta::annotations_of_with_type(entity, ^^tag::field);
            const auto atoms = annotation_count(entity, ^^tag::atomic);
            const auto containers = std::meta::annotations_of_with_type(entity, ^^tag::container);
            const auto conversions = std::meta::annotations_of_with_type(entity, ^^conversion_tag);
            const auto transient = annotation_count(entity, ^^tag::transient);
            const auto bypass = annotation_count(entity, ^^tag::bypass_pointer_check);
            const auto streams = annotation_count(entity, ^^tag::auto_stream);
            const bool automatic = stream || sequence;
            const bool serialized = automatic || fields.size() == 1;
            const bool base = std::meta::is_base(entity);
            const auto declared = std::meta::type_of(entity);
            const auto memory = std::meta::remove_cvref(declared);
            if (streams > 1 || (streams == 1 && (!serialized || transient != 0 || atoms != 0 || !containers.empty() || bypass != 0))
                || transient > 1 || (transient == 1 && (!automatic || (!stream && !fields.empty()) || atoms != 0
                    || !containers.empty() || bypass != 0 || !conversions.empty()))
                || (!stream && fields.size() > 1) || atoms > 1 || bypass > 1 || conversions.size() > 1
                || (conversions.size() == 1 && (!serialized || atoms != 0 || bypass != 0 || base))
                || containers.size() > 1 || (containers.size() == 1 && (!serialized || bypass != 0))
                || (bypass == 1 && !automatic && atoms == 0)) {
                valid = false;
                return std::nullopt;
            }
            if (transient == 1) {
                return std::nullopt;
            }
            const bool forced = schema_forced_atomic(owner, entity);
            if (atoms == 1 && (!serialized || !containers.empty() || !forced)) {
                valid = false;
                return std::nullopt;
            }
            if (!serialized) {
                return std::nullopt;
            }
            schema_node node{
                .role = base ? schema_role::base : schema_role::member,
                .type = memory, .memory_type = memory, .entity = entity,
            };
            if (!stream) {
                if (fields.size() == 1) {
                    next_id = std::meta::extract<tag::field>(fields.front()).id;
                }
                if (next_id > std::numeric_limits<id_type>::max()) {
                    valid = false;
                    return std::nullopt;
                }
                node.id = static_cast<id_type>(next_id++);
            }
            if (std::meta::is_bit_field(entity) || (base && std::meta::is_virtual(entity))) {
                valid = false;
                return std::nullopt;
            }
            if (conversions.size() == 1) {
                node.conversion = std::meta::extract<conversion_tag>(conversions.front());
                const auto storage = node.conversion.target_type;
                if (!std::meta::is_type(storage) || std::meta::is_reference_type(declared)
                    || std::meta::is_const_type(declared) || std::meta::is_volatile_type(declared)
                    || !std::meta::is_same_type(storage, std::meta::remove_cvref(storage))) {
                    valid = false;
                    return std::nullopt;
                }
                if (!std::meta::extract<bool>(std::meta::substitute(^^schema_conversion_valid_v, {memory, storage}))
                    || (node.conversion.handler_id == nullptr
                        && !std::meta::extract<bool>(std::meta::substitute(^^schema_cast_valid_v, {memory, storage})))) {
                    valid = false;
                    return std::nullopt;
                }
                node.type = storage;
            } else if (forced) {
                node.representation = atomic_representation::forced;
            } else if (std::meta::is_reference_type(declared) || std::meta::is_pointer_type(memory)) {
                valid = false;
                return std::nullopt;
            }
            if (!containers.empty()) {
                if (!schema_facts(node.type).container) {
                    valid = false;
                    return std::nullopt;
                }
                node.duplicates = std::meta::extract<tag::container>(containers.front()).duplicates == duplicate_policy::reject
                    ? schema_duplicates::reject : schema_duplicates::deduplicate;
            }
            return node;
        }
    };

    consteval schema_tree build_schema(std::meta::info type, bool stream = false) {
        type = recursively_dealias(std::meta::remove_cvref(type));
        schema_builder builder;
        builder.nodes.push_back(schema_node{.type = type, .memory_type = type});
        const auto payload = builder.append(schema_node{.type = type, .memory_type = type}, stream);
        auto &root = builder.nodes.front();
        const auto &value = builder.nodes[payload];
        root.children_begin = builder.edges.size();
        root.children_count = 1;
        root.wire_kind = value.wire_kind;
        root.mapping = value.mapping;
        root.mapping_group_size = value.mapping_group_size;
        root.type_hash = value.type_hash;
        root.header_size = value.kind == schema_kind::atomic ? entry_head_wire_size : 0;
        root.encode_extent = builder.add_size(root.header_size, value.encode_extent);
        builder.edges.push_back(payload);
        return {builder.valid, std::define_static_array(builder.nodes), std::define_static_array(builder.edges), 0};
    }
}
