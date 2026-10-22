#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <meta>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "model.hpp"
#include "wire_contract.hpp"
#include "../codec/reflected.hpp"
#include "../container.hpp"

namespace srl::detail {
    enum class field_mode { explicit_record, automatic_record, stream };

    struct field_build_context {
        const std::meta::info owner;
        const field_mode mode;
        std::uint64_t next_id{};

        consteval bool stream() const { return mode == field_mode::stream; }
        consteval bool automatic() const { return mode != field_mode::explicit_record; }
    };

    struct field_metadata {
        const std::meta::info entity;
        const entity_facts facts;
        const std::meta::info memory;

        consteval explicit field_metadata(std::meta::info entity)
            : entity(entity), facts(entity), memory(std::meta::remove_cvref(facts.declared)) {}

        // Call only after validating that exactly one annotation is present.
        consteval std::meta::info annotation(std::meta::info type) const {
            return std::meta::annotations_of_with_type(entity, type).front();
        }
    };

    consteval bool schema_forced_leaf(const field_metadata &field, const field_build_context &context) {
        const auto &facts = field.facts;
        const auto declared = facts.declared;
        const auto type = field.memory;
        const bool bypass = annotation_count(context.owner, ^^tag::bypass_pointer_check) == 1
                            || facts.bypass == 1;
        if (std::meta::is_reference_type(declared) || facts.unsupported
            || facts.conversions != 0
            || facts.containers != 0
            || (facts.leaves != 1 && !(bypass && schema_contains_pointer(type)))) {
            return false;
        }
        const auto options = facts.leaves == 1
            ? std::meta::extract<tag::leaf>(field.annotation(^^tag::leaf)) : tag::leaf{};
        return std::meta::is_trivially_copyable_type(type)
               && (options.allow_padding || std::meta::has_unique_object_representations(type))
               && (bypass || !schema_contains_pointer(type));
    }

    // These bridges instantiate user-provided C++ customization interfaces only.
    // Tree traversal, annotation processing and size arithmetic remain data operations.
    struct schema_type_facts {
        bool leaf{};
        bool container{};
        // Raw customization-interface value; validate in the builder's evaluation
        // so overflow can be reported with the enclosing schema path.
        szt codec_extent = std::dynamic_extent;
        std::size_t count = std::dynamic_extent;
        std::uint8_t group{};
        std::meta::info first{};
        std::meta::info second{};
        leaf_representation representation = leaf_representation::codec;
        bool has_object_layout{};
        bool builtin_scalar_codec{};
        bool builtin_object_codec{};
    };

    template<typename T>
    inline constexpr schema_type_facts schema_type_facts_v = [] consteval {
        schema_type_facts result;
        if constexpr (leaf_serializable<T>) {
            result.leaf = true;
            result.codec_extent = leaf_codec<T>::fixed_size;
            if constexpr (primitive<T>) {
                result.builtin_scalar_codec = std::is_base_of_v<scalar_codec<T>, leaf_codec<T>>;
            } else if constexpr (auto_leaf_supported(^^T)) {
                result.builtin_object_codec = std::is_base_of_v<object_codec<T>, leaf_codec<T>>;
            }
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

        consteval std::size_t append(schema_node node, bool stream = false) {
            path.push_back(node.entity != std::meta::info{} ? node.entity : node.type);
            if (node.converted()) { path.push_back(node.type); }
            std::size_t index;
            try {
                index = append_value(node, stream);
            } catch (const extent_overflow &failure) {
                reject(schema_error::extent_overflow, {}, failure.what());
            }
            if (node.converted()) { path.pop_back(); }
            path.pop_back();
            return index;
        }

        consteval std::size_t append_value(schema_node node, bool stream) {
            const auto index = nodes.size();
            nodes.push_back(node);
            const auto type = node.type;
            if (annotation_count(type, ^^tag::bypass_validation) > 1) {
                reject(schema_error::conflicting_annotations, type, "duplicate bypass_validation");
            }
            node.validation = validation_for(type, node.validation);
            if (auto issue = enum_metadata_issue(type); issue.reason != schema_error::none) {
                reject(issue.reason, type, issue.detail);
            }
            if (node.representation == leaf_representation::forced) {
                build_forced_leaf(node);
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
                build_codec_leaf(node, facts);
                nodes[index] = node;
                return index;
            }

            ancestors.push_back(type);
            std::vector<std::size_t> children;
            node.header_size = schema_extent::fixed(entry_head_wire_size);
            if (facts.container) {
                build_container(node, facts, children);
            } else if ((!stream && !schema_has_fields(type)) || seq_count > 1
                       || annotation_count(type, ^^tag::bypass_pointer_check) > 1
                       || (annotation_count(type, ^^tag::bypass_pointer_check) != 0 && !stream && seq_count != 1)) {
                reject(schema_error::unsupported_type, type);
            } else {
                build_record(node, stream ? field_mode::stream
                    : (seq_count == 1 ? field_mode::automatic_record : field_mode::explicit_record), children);
            }
            ancestors.pop_back();
            finish_composite(node, children);
            nodes[index] = node;
            return index;
        }

        consteval void build_forced_leaf(schema_node &node) {
            const auto type = node.type;
            std::vector<object_field> fields;
            collect_object_fields(type, {}, 0, fields, path, node.validation);
            node.kind = schema_kind::leaf;
            node.encode_extent = node.decode_extent = schema_extent::fixed(std::meta::size_of(type));
            node.object = make_object_layout(type, false, node.validation);
            node.wire_type = leaf_wire_type(node);
        }

        consteval void build_codec_leaf(schema_node &node, const schema_type_facts &facts) {
            const auto type = node.type;
            node.kind = schema_kind::leaf;
            node.encode_extent = node.decode_extent = schema_extent::fixed(facts.codec_extent);
            node.representation = facts.representation;
            node.builtin_scalar_codec = facts.builtin_scalar_codec;
            node.builtin_object_codec = facts.builtin_object_codec;
            if (facts.has_object_layout) {
                // Validate here before entering the codec's separate constant initializer,
                // so inspection can catch failures with the enclosing schema path.
                std::vector<object_field> fields;
                collect_object_fields(type, {}, 0, fields, path, node.validation);
                if (facts.builtin_object_codec) {
                    node.object = make_object_layout(type,
                        facts.representation == leaf_representation::segmented, node.validation);
                } else {
                    node.object = *std::meta::extract<const object_layout *>(
                        std::meta::substitute(^^codec_object_layout, {type}));
                }
            }
            node.wire_type = leaf_wire_type(node);
        }

        consteval void build_container(schema_node &node, const schema_type_facts &facts, std::vector<std::size_t> &children) {
            const auto type = node.type;
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
                .validation = node.validation,
            }));
            if ((facts.count == std::dynamic_extent || facts.group == 2)
                && !schema_default_initializable(facts.first)) {
                reject(schema_error::missing_default_constructor, facts.first);
            }
            if (facts.group == 2) {
                children.push_back(append(schema_node{
                    .role = schema_role::mapped,
                    .type = std::meta::remove_cvref(facts.second), .memory_type = facts.second,
                    .validation = node.validation,
                }));
                if (!schema_default_initializable(facts.second)) {
                    reject(schema_error::missing_default_constructor, facts.second);
                }
            }
            auto stride = schema_extent::fixed(entry_mapping_wire_size).scaled_by(facts.group);
            for (auto child: children) {
                stride += nodes[child].encode_extent;
            }
            const auto payload = facts.count == std::dynamic_extent
                ? stride.scaled_by_dynamic_count() : stride.scaled_by(facts.count);
            node.encode_extent = schema_extent::fixed(entry_head_wire_size) + payload;
        }

        consteval void build_record(schema_node &node, field_mode mode, std::vector<std::size_t> &children) {
            const auto type = node.type;
            const bool stream = mode == field_mode::stream;
            node.kind = stream ? schema_kind::stream : schema_kind::record;
            node.wire_kind = stream ? entry_kind::stream : entry_kind::record;
            node.mapping_group_size = stream ? 0 : 1;
            field_build_context context{.owner = type, .mode = mode};
            for (auto entity: reflected_entities(type)) {
                auto child = field(entity, context);
                if (child) {
                    child->validation = validation_for(entity, node.validation);
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
            node.encode_extent = schema_extent::fixed(entry_head_wire_size)
                + (stream ? schema_extent::fixed(0)
                          : schema_extent::fixed(entry_mapping_wire_size).scaled_by(children.size()));
            for (auto child: children) {
                node.encode_extent += nodes[child].encode_extent;
            }
        }

        consteval void finish_composite(schema_node &node, std::span<const std::size_t> children) {
            node.children_begin = edges.size();
            node.children_count = children.size();
            node.wire_type = hash_integer(fnv1a32_offset, std::to_underlying(node.wire_kind));
            if (node.kind != schema_kind::record) {
                for (auto child: children) {
                    node.wire_type = hash_integer(node.wire_type, nodes[child].wire_type);
                    if (node.kind == schema_kind::stream && nodes[child].entity != std::meta::info{}) {
                        const auto entity = nodes[child].entity;
                        node.wire_type = hash_string(node.wire_type, std::meta::is_base(entity)
                            ? std::meta::display_string_of(std::meta::type_of(entity))
                            : std::meta::identifier_of(entity));
                    }
                }
            }
            edges.insert(edges.end(), children.begin(), children.end());
        }

        consteval void validate_field_annotations(const field_metadata &metadata, const field_build_context &context) {
            const auto &facts = metadata.facts;
            const auto entity = metadata.entity;
            const bool stream = context.stream();
            const bool automatic = context.automatic();
            const bool serialized = automatic || facts.fields == 1;

            if (facts.validation_bypasses > 1
                || (facts.validation_bypasses != 0 && (!serialized || facts.transient != 0))) {
                reject(schema_error::conflicting_annotations, entity);
            }

            if (facts.fallbacks > 1 || (facts.fallbacks != 0 && (stream || !serialized || facts.transient != 0))
                || (facts.fallbacks == 1 && !fallback_valid(entity))) {
                reject(schema_error::conflicting_annotations, entity);
            }
            if (facts.streams > 1 || (facts.streams == 1
                && (!serialized || facts.transient != 0 || facts.leaves != 0
                    || facts.containers != 0 || facts.bypass != 0))) {
                reject(schema_error::conflicting_annotations, entity);
            }
            if (facts.transient > 1 || (facts.transient == 1
                && (!automatic || (!stream && facts.fields != 0) || facts.leaves != 0
                    || facts.containers != 0 || facts.bypass != 0 || facts.conversions != 0))) {
                reject(schema_error::conflicting_annotations, entity);
            }
            if ((!stream && facts.fields > 1) || facts.leaves > 1 || facts.bypass > 1 || facts.conversions > 1
                || (facts.conversions == 1 && (!serialized || facts.leaves != 0 || facts.bypass != 0 || facts.base))
                || facts.containers > 1 || (facts.containers == 1 && (!serialized || facts.bypass != 0))
                || (facts.bypass == 1 && !automatic && facts.leaves == 0)) {
                reject(schema_error::conflicting_annotations, entity);
            }
        }

        consteval void assign_field_id(schema_node &node, const field_metadata &metadata, field_build_context &context) {
            const auto &facts = metadata.facts;
            const auto entity = metadata.entity;
            if (!context.stream()) {
                node.has_field_id = true;
                if (facts.fields == 1) {
                    context.next_id = std::meta::extract<tag::field>(metadata.annotation(^^tag::field)).id;
                }
                if (context.next_id > std::numeric_limits<id_type>::max()) {
                    reject(schema_error::field_id_overflow, entity);
                }
                node.id = static_cast<id_type>(context.next_id++);
            }
        }

        consteval void apply_conversion(schema_node &node, const field_metadata &metadata) {
            const auto &facts = metadata.facts;
            const auto entity = metadata.entity;
            node.conversion = std::meta::extract<conversion_tag>(metadata.annotation(^^conversion_tag));
            const auto storage = node.conversion.target_type;
            if (!std::meta::is_type(storage) || std::meta::is_reference_type(facts.declared)
                || std::meta::is_const_type(facts.declared) || std::meta::is_volatile_type(facts.declared)
                || !std::meta::is_same_type(storage, std::meta::remove_cvref(storage))) {
                reject(schema_error::invalid_conversion, entity);
            }
            if (!schema_conversion_valid(metadata.memory, storage)
                || (node.conversion.handler_id == nullptr
                    && !std::meta::extract<bool>(std::meta::substitute(^^schema_cast_valid_v, {metadata.memory, storage})))) {
                reject(schema_error::invalid_conversion, entity);
            }
            node.type = storage;
        }

        consteval std::optional<schema_node> field(std::meta::info entity, field_build_context &context) {
            const field_metadata metadata{entity};
            const auto &facts = metadata.facts;
            validate_field_annotations(metadata, context);
            if (facts.transient == 1) {
                return std::nullopt;
            }
            const bool serialized = context.automatic() || facts.fields == 1;
            const bool forced = schema_forced_leaf(metadata, context);
            if (facts.leaves == 1 && (!serialized || facts.containers != 0 || !forced)) {
                reject(schema_error::invalid_object_representation, entity);
            }
            if (!serialized) {
                return std::nullopt;
            }

            schema_node node{
                .role = facts.base ? schema_role::base : schema_role::member,
                .type = metadata.memory, .memory_type = metadata.memory, .entity = entity,
            };
            if (facts.fallbacks == 1) {
                node.fallback_type = fallback_annotation_type(entity);
            }
            assign_field_id(node, metadata, context);
            if (facts.unsupported) {
                reject(schema_error::unsupported_entity, entity);
            }

            if (facts.conversions == 1) {
                apply_conversion(node, metadata);
            } else if (forced) {
                node.representation = leaf_representation::forced;
            } else if (std::meta::is_reference_type(facts.declared) || std::meta::is_pointer_type(metadata.memory)) {
                reject(schema_error::unsupported_entity, entity);
            }
            if (facts.containers == 1) {
                if (!schema_facts(node.type).container) {
                    reject(schema_error::conflicting_annotations, entity);
                }
                node.duplicates = std::meta::extract<tag::container>(metadata.annotation(^^tag::container)).duplicates == duplicate_policy::reject
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
        root.header_size = schema_extent::fixed(value.kind == schema_kind::leaf ? entry_head_wire_size : 0);
        builder.path.push_back(type);
        try {
            root.encode_extent = root.header_size + value.encode_extent;
        } catch (const extent_overflow &failure) {
            builder.reject(schema_error::extent_overflow, {}, failure.what());
        }
        builder.edges.push_back(payload);
        return {std::define_static_array(builder.nodes), std::define_static_array(builder.edges), 0};
    }
}
