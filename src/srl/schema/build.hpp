#pragma once

#include <algorithm>
#include <concepts>
#include <limits>
#include <meta>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "model.hpp"
#include "wire_contract.hpp"
#include "../codec/reflected.hpp"
#include "../container.hpp"

namespace srl::detail {
    consteval bool schema_forced_leaf(std::meta::info owner, std::meta::info entity) {
        const entity_facts facts{entity};
        const auto declared = facts.declared;
        const auto type = std::meta::remove_cvref(declared);
        const bool bypass = annotation_count(owner, ^^tag::bypass_pointer_check) == 1
                            || facts.bypass == 1;
        const auto leaves = std::meta::annotations_of_with_type(entity, ^^tag::leaf);
        if (std::meta::is_reference_type(declared) || facts.unsupported
            || facts.conversions != 0
            || facts.containers != 0
            || (leaves.size() != 1 && !(bypass && schema_contains_pointer(type)))) {
            return false;
        }
        const auto options = leaves.size() == 1 ? std::meta::extract<tag::leaf>(leaves.front()) : tag::leaf{};
        return std::meta::is_trivially_copyable_type(type)
               && (options.allow_padding || std::meta::has_unique_object_representations(type))
               && (bypass || !schema_contains_pointer(type));
    }

    // These bridges instantiate user-provided C++ customization interfaces only.
    // Tree traversal, annotation processing and size arithmetic remain data operations.
    struct schema_type_facts {
        bool leaf{};
        bool container{};
        szt extent = std::dynamic_extent;
        std::size_t count = std::dynamic_extent;
        std::uint8_t group{};
        std::meta::info first{};
        std::meta::info second{};
        leaf_representation representation = leaf_representation::codec;
        bool has_object_layout{};
    };

    template<typename T>
    inline constexpr schema_type_facts schema_type_facts_v = [] consteval {
        schema_type_facts result;
        if constexpr (leaf_serializable<T>) {
            result.leaf = true;
            result.extent = leaf_codec<T>::fixed_size;
            if constexpr (requires { leaf_codec<T>::layout; }) {
                result.has_object_layout = true;
            }
            if constexpr (requires { leaf_codec<T>::uses_direct_object_representation; }) {
                if (leaf_codec<T>::uses_direct_object_representation) {
                    result.representation = leaf_representation::direct;
                }
            }
            if constexpr (requires { leaf_codec<T>::uses_segmented_object_representation; }) {
                if (leaf_codec<T>::uses_segmented_object_representation) {
                    result.representation = leaf_representation::segmented;
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

    template<typename T>
    inline constexpr const object_layout *codec_object_layout = &leaf_codec<T>::layout;

    consteval bool schema_conversion_valid(std::meta::info memory, std::meta::info storage) {
        const auto rvalue = std::meta::add_rvalue_reference(storage);
        return std::meta::extract<bool>(std::meta::substitute(^^std::default_initializable, {storage}))
            && std::meta::is_nothrow_destructible_type(storage)
            && std::meta::is_constructible_type(storage, {rvalue})
            && std::meta::is_convertible_type(rvalue, storage)
            && std::meta::is_move_assignable_type(memory);
    }

    template<typename Memory, typename Storage>
    inline constexpr bool schema_cast_valid_v = requires(const Memory &mem, Storage &&storage, Memory &out) {
        static_cast<Storage>(mem);
        out = static_cast<Memory>(std::move(storage));
    };

    consteval bool schema_default_initializable(std::meta::info type) {
        return std::meta::extract<bool>(std::meta::substitute(^^std::default_initializable, {type}));
    }

    struct schema_builder {
        std::vector<schema_node> nodes;
        std::vector<std::size_t> edges;
        std::vector<std::meta::info> ancestors;
        std::vector<std::meta::info> path;

        [[noreturn]] consteval void reject(schema_error reason, std::meta::info entity = {},
                                           std::string_view detail = {}) {
            reject_schema(reason, path, entity, detail);
        }

        consteval szt add_size(szt left, szt right) {
            if (left == std::dynamic_extent || right == std::dynamic_extent) {
                return std::dynamic_extent;
            }
            constexpr auto maximum = maximum_entry_size;
            if (left > maximum || right > maximum - left) {
                reject(schema_error::extent_overflow, {}, "serialized extent exceeds maximum_entry_size");
            }
            return left + right;
        }

        consteval szt multiply_size(std::size_t count, szt size) {
            if (count == 0 || size == 0) {
                return 0;
            }
            if (count == std::dynamic_extent || size == std::dynamic_extent) {
                return std::dynamic_extent;
            }
            if (count > maximum_entry_size / size) {
                reject(schema_error::extent_overflow, {}, "serialized extent exceeds maximum_entry_size");
            }
            return count * size;
        }

        consteval std::size_t append(schema_node node, bool stream = false) {
            path.push_back(node.entity != std::meta::info{} ? node.entity : node.type);
            if (node.converted()) { path.push_back(node.type); }
            const auto index = append_value(node, stream);
            if (node.converted()) { path.pop_back(); }
            path.pop_back();
            return index;
        }

        consteval std::size_t append_value(schema_node node, bool stream) {
            const auto index = nodes.size();
            nodes.push_back(node);
            const auto type = node.type;
            if (auto issue = enum_metadata_issue(type); issue.reason != schema_error::none) {
                reject(issue.reason, type, issue.detail);
            }
            if (node.representation == leaf_representation::forced) {
                std::vector<object_field> fields;
                collect_object_fields(type, {}, 0, fields, path);
                node.kind = schema_kind::leaf;
                node.encode_extent = node.decode_extent = std::meta::size_of(type);
                node.object = make_object_layout(type);
                node.wire_type = leaf_wire_type(node);
                nodes[index] = node;
                return index;
            }
            if (std::ranges::find(ancestors, type) != ancestors.end()) {
                reject(schema_error::recursive_type, type);
            }
            const auto seq_count = annotation_count(type, ^^tag::auto_seq);
            const auto stream_count = annotation_count(type, ^^tag::auto_stream);
            const auto leaf_count = annotation_count(type, ^^tag::leaf);
            stream = stream || stream_count != 0;
            const auto facts = schema_facts(type);
            if (stream_count > 1 || (stream_count != 0 && seq_count != 0)
                || (stream && (!std::meta::is_class_type(type) || facts.leaf || facts.container || leaf_count != 0))
                || leaf_count > 1) {
                reject(schema_error::conflicting_annotations, type);
            }
            if (leaf_count != 0 && (!facts.leaf || leaf_metadata_issue(type) != std::meta::info{})) {
                const auto entity = leaf_metadata_issue(type);
                reject(schema_error::invalid_object_representation, entity == std::meta::info{} ? type : entity);
            }
            if (facts.leaf) {
                node.kind = schema_kind::leaf;
                node.encode_extent = node.decode_extent = facts.extent;
                node.representation = facts.representation;
                if (facts.has_object_layout) {
                    // Validate here before entering the codec's separate constant initializer,
                    // so inspection can catch failures with the enclosing schema path.
                    std::vector<object_field> fields;
                    collect_object_fields(type, {}, 0, fields, path);
                    node.object = *std::meta::extract<const object_layout *>(
                        std::meta::substitute(^^codec_object_layout, {type}));
                }
                if (facts.extent > maximum_entry_size) {
                    reject(schema_error::extent_overflow, type);
                }
                node.wire_type = leaf_wire_type(node);
                nodes[index] = node;
                return index;
            }

            ancestors.push_back(type);
            std::vector<std::size_t> children;
            node.header_size = entry_head_wire_size;
            if (facts.container) {
                node.kind = schema_kind::container;
                node.wire_kind = facts.group == 1 ? entry_kind::sequence : entry_kind::map;
                node.mapping_group_size = facts.group;
                node.element_count = facts.count;
                if (facts.count != std::dynamic_extent
                    && facts.count > entry_metadata::max_mapping_count / facts.group) {
                    reject(schema_error::mapping_limit, type);
                }
                children.push_back(append(schema_node{
                    .role = facts.group == 1 ? schema_role::element : schema_role::key,
                    .type = std::meta::remove_cvref(facts.first), .memory_type = facts.first,
                }));
                if ((facts.count == std::dynamic_extent || facts.group == 2)
                    && !schema_default_initializable(facts.first)) {
                    reject(schema_error::missing_default_constructor, facts.first);
                }
                if (facts.group == 2) {
                    children.push_back(append(schema_node{
                        .role = schema_role::mapped,
                        .type = std::meta::remove_cvref(facts.second), .memory_type = facts.second,
                    }));
                    if (!schema_default_initializable(facts.second)) {
                        reject(schema_error::missing_default_constructor, facts.second);
                    }
                }
                szt stride = facts.group * entry_mapping_wire_size;
                for (auto child: children) {
                    stride = add_size(stride, nodes[child].encode_extent);
                }
                node.encode_extent = add_size(entry_head_wire_size, multiply_size(facts.count, stride));
            } else if ((!stream && !schema_has_fields(type)) || seq_count > 1
                       || annotation_count(type, ^^tag::bypass_pointer_check) > 1
                       || (annotation_count(type, ^^tag::bypass_pointer_check) != 0 && !stream && seq_count != 1)) {
                reject(schema_error::unsupported_type, type);
            } else {
                node.kind = stream ? schema_kind::stream : schema_kind::record;
                node.wire_kind = stream ? entry_kind::stream : entry_kind::record;
                node.mapping_group_size = stream ? 0 : 1;
                std::uint64_t next_id{};
                for (auto entity: reflected_entities(type)) {
                    auto child = field(type, entity, stream, seq_count == 1, next_id);
                    if (child) {
                        children.push_back(append(*child, annotation_count(entity, ^^tag::auto_stream) == 1));
                    }
                }
                if (!stream) {
                    if (children.size() > entry_metadata::max_mapping_count) {
                        reject(schema_error::mapping_limit, type);
                    }
                    std::ranges::sort(children, [&](auto left, auto right) { return nodes[left].id < nodes[right].id; });
                    for (std::size_t i = 1; i < children.size(); ++i) {
                        if (nodes[children[i - 1]].id == nodes[children[i]].id) {
                            reject(schema_error::duplicate_field_id, nodes[children[i]].entity,
                                std::string("field id=") + diagnostic_integer(nodes[children[i]].id)
                                + " also used by " + std::string(std::meta::display_string_of(nodes[children[i - 1]].entity))
                                + "; assign distinct field IDs");
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
            node.wire_type = hash_integer(fnv1a32_offset, std::to_underlying(node.wire_kind));
            if (node.kind != schema_kind::record) {
                for (auto child: children) {
                    node.wire_type = hash_integer(node.wire_type, nodes[child].wire_type);
                    if (stream && nodes[child].entity != std::meta::info{}) {
                        const auto entity = nodes[child].entity;
                        node.wire_type = hash_string(node.wire_type, std::meta::is_base(entity)
                            ? std::meta::display_string_of(std::meta::type_of(entity))
                            : std::meta::identifier_of(entity));
                    }
                }
            }
            edges.insert(edges.end(), children.begin(), children.end());
            nodes[index] = node;
            return index;
        }

        consteval std::optional<schema_node> field(std::meta::info owner, std::meta::info entity,
                                                   bool stream, bool sequence, std::uint64_t &next_id) {
            const auto fields = std::meta::annotations_of_with_type(entity, ^^tag::field);
            const entity_facts facts{entity};
            const auto leaves = facts.leaves;
            const auto containers = std::meta::annotations_of_with_type(entity, ^^tag::container);
            const auto conversions = std::meta::annotations_of_with_type(entity, ^^conversion_tag);
            const auto transient = facts.transient;
            const auto bypass = facts.bypass;
            const auto streams = facts.streams;
            const auto fallbacks = facts.fallbacks;
            const bool automatic = stream || sequence;
            const bool serialized = automatic || facts.fields == 1;
            const bool base = facts.base;
            const auto declared = facts.declared;
            const auto memory = std::meta::remove_cvref(declared);
            if (fallbacks > 1 || (fallbacks != 0 && (stream || !serialized || transient != 0))
                || (fallbacks == 1 && !fallback_valid(entity))
                || streams > 1 || (streams == 1 && (!serialized || transient != 0 || leaves != 0 || !containers.empty() || bypass != 0))
                || transient > 1 || (transient == 1 && (!automatic || (!stream && !fields.empty()) || leaves != 0
                    || !containers.empty() || bypass != 0 || !conversions.empty()))
                || (!stream && facts.fields > 1) || leaves > 1 || bypass > 1 || facts.conversions > 1
                || (facts.conversions == 1 && (!serialized || leaves != 0 || bypass != 0 || base))
                || facts.containers > 1 || (facts.containers == 1 && (!serialized || bypass != 0))
                || (bypass == 1 && !automatic && leaves == 0)) {
                reject(schema_error::conflicting_annotations, entity);
            }
            if (transient == 1) {
                return std::nullopt;
            }
            const bool forced = schema_forced_leaf(owner, entity);
            if (leaves == 1 && (!serialized || !containers.empty() || !forced)) {
                reject(schema_error::invalid_object_representation, entity);
            }
            if (!serialized) {
                return std::nullopt;
            }
            schema_node node{
                .role = base ? schema_role::base : schema_role::member,
                .type = memory, .memory_type = memory, .entity = entity,
            };
            if (fallbacks == 1) {
                node.fallback_type = fallback_annotation_type(entity);
            }
            if (!stream) {
                node.has_field_id = true;
                if (facts.fields == 1) {
                    next_id = std::meta::extract<tag::field>(fields.front()).id;
                }
                if (next_id > std::numeric_limits<id_type>::max()) {
                    reject(schema_error::field_id_overflow, entity);
                }
                node.id = static_cast<id_type>(next_id++);
            }
            if (facts.unsupported) {
                reject(schema_error::unsupported_entity, entity);
            }
            if (facts.conversions == 1) {
                node.conversion = std::meta::extract<conversion_tag>(conversions.front());
                const auto storage = node.conversion.target_type;
                if (!std::meta::is_type(storage) || std::meta::is_reference_type(declared)
                    || std::meta::is_const_type(declared) || std::meta::is_volatile_type(declared)
                    || !std::meta::is_same_type(storage, std::meta::remove_cvref(storage))) {
                    reject(schema_error::invalid_conversion, entity);
                }
                if (!schema_conversion_valid(memory, storage)
                    || (node.conversion.handler_id == nullptr
                        && !std::meta::extract<bool>(std::meta::substitute(^^schema_cast_valid_v, {memory, storage})))) {
                    reject(schema_error::invalid_conversion, entity);
                }
                node.type = storage;
            } else if (forced) {
                node.representation = leaf_representation::forced;
            } else if (std::meta::is_reference_type(declared) || std::meta::is_pointer_type(memory)) {
                reject(schema_error::unsupported_entity, entity);
            }
            if (!containers.empty()) {
                if (!schema_facts(node.type).container) {
                    reject(schema_error::conflicting_annotations, entity);
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
        root.mapping_group_size = value.mapping_group_size;
        root.wire_type = value.wire_type;
        root.header_size = value.kind == schema_kind::leaf ? entry_head_wire_size : 0;
        builder.path.push_back(type);
        root.encode_extent = builder.add_size(root.header_size, value.encode_extent);
        builder.edges.push_back(payload);
        return {std::define_static_array(builder.nodes), std::define_static_array(builder.edges), 0};
    }
}
