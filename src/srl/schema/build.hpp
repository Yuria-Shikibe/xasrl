#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <meta>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "annotation_targets.hpp"
#include "model.hpp"
#include "wire_contract.hpp"
#include "../codec/reflected.hpp"
#include "../container.hpp"
#include "../conversion.hpp"

namespace xasrl::detail {
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
               && (options.allow_non_unique_presentation || std::meta::has_unique_object_representations(type))
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
        leaf_strategy strategy = leaf_strategy::custom;
    };

    template<typename T>
    inline constexpr schema_type_facts schema_type_facts_v = [] consteval {
        schema_type_facts result;
        if constexpr (leaf_serializable<T>) {
            result.leaf = true;
            result.codec_extent = leaf_codec<T>::fixed_size;
            if constexpr (primitive<T>) {
                if constexpr (std::is_base_of_v<scalar_codec<T>, leaf_codec<T>>) {
                    result.strategy = leaf_strategy::scalar_checked;
                }
            } else if constexpr (auto_leaf_supported(^^T)) {
                if constexpr (std::is_base_of_v<object_codec<T>, leaf_codec<T>>) {
                    result.strategy = leaf_strategy::object;
                }
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

    struct schema_seed {
        std::meta::info type;
        schema_binding binding{};
        validation_mode validation = validation_mode::checked;
        schema_duplicates duplicates = schema_duplicates::inherit;
        bool forced{};
    };

    struct schema_builder {
        std::vector<std::optional<schema_node_storage>> nodes;
        std::vector<std::size_t> edges;
        std::vector<std::meta::info> ancestors;
        std::vector<std::meta::info> path;

        [[noreturn]] consteval void reject(schema_error reason, std::meta::info entity = {},
                                           std::string_view detail = {}) {
            reject_schema(reason, path, entity, detail);
        }

        consteval std::size_t append(schema_seed seed, bool stream = false) {
            const bool field = seed.binding.is_field();
            const bool converted = field && seed.binding.field().conversion != schema_conversion_kind::none;
            path.push_back(field ? seed.binding.field().entity : seed.type);
            if (converted) { path.push_back(seed.type); }
            std::size_t index;
            try {
                index = append_value(seed, stream);
            } catch (const extent_overflow &failure) {
                reject(schema_error::extent_overflow, {}, failure.what());
            }
            if (converted) { path.pop_back(); }
            path.pop_back();
            return index;
        }

        consteval std::size_t append_value(schema_seed seed, bool stream) {
            const auto index = nodes.size();
            nodes.emplace_back(std::nullopt);
            const auto type = seed.type;
#if defined(SRL_VALIDATE_ANNOTATION_TARGETS) && SRL_VALIDATE_ANNOTATION_TARGETS
            validate_type_annotation_targets(recursively_dealias(type), path);
#endif
            const auto type_inline = annotation_count(type, ^^tag::inline_mapping);
            const auto field_inline = seed.binding.is_field()
                ? annotation_count(seed.binding.field().entity, ^^tag::inline_mapping) : 0;
            const bool forced_inline = type_inline != 0 || field_inline != 0;
            if (type_inline > 1 || field_inline > 1) {
                reject(schema_error::conflicting_annotations, type, "duplicate inline_mapping");
            }
            if (annotation_count(type, ^^tag::bypass_validation) > 1) {
                reject(schema_error::conflicting_annotations, type, "duplicate bypass_validation");
            }
            seed.validation = validation_for(type, seed.validation);
            if (auto issue = enum_metadata_issue(type); issue.reason != schema_error::none) {
                reject(issue.reason, type, issue.detail);
            }
            if (seed.forced) {
                if (forced_inline) { reject(schema_error::conflicting_annotations, type, "inline_mapping requires a composite value"); }
                nodes[index] = build_forced_leaf(seed);
                return index;
            }
            if (std::ranges::find(ancestors, type) != ancestors.end()) {
                reject(schema_error::recursive_type, type);
            }
            const auto seq_count = annotation_count(type, ^^tag::auto_seq);
            const auto stream_count = annotation_count(type, ^^tag::auto_stream);
            const auto leaf_count = annotation_count(type, ^^tag::leaf);
            if (seq_count > 1 || stream_count > 1 || leaf_count > 1
                || (seq_count != 0 && (stream_count != 0 || leaf_count != 0))
                || ((seq_count != 0 || stream_count != 0 || leaf_count != 0)
                    && !std::meta::is_class_type(type))) {
                reject(schema_error::conflicting_annotations, type);
            }
            stream = stream || stream_count != 0;
            const auto facts = schema_facts(type);
            if (stream && (!std::meta::is_class_type(type) || facts.leaf || facts.container || leaf_count != 0)) {
                reject(schema_error::conflicting_annotations, type);
            }
            if (leaf_count != 0) {
                if (const auto entity = leaf_metadata_issue(type); entity != std::meta::info{}) {
                    reject(schema_error::invalid_object_representation, entity);
                }
                if (!facts.leaf) {
                    reject_automatic_leaf(type);
                }
            }
            if (facts.leaf) {
                if (forced_inline) { reject(schema_error::conflicting_annotations, type, "inline_mapping requires a composite value"); }
                nodes[index] = build_codec_leaf(seed, facts);
                return index;
            }

            // Recursive appends can reallocate nodes; reacquire the reserved slot after building.
            ancestors.push_back(type);
            std::vector<std::size_t> children;
            if (facts.container) {
                const auto node = build_container(seed, facts, children);
                nodes[index] = node;
            } else if ((!stream && !schema_has_fields(type))
                       || annotation_count(type, ^^tag::bypass_pointer_check) > 1
                       || (annotation_count(type, ^^tag::bypass_pointer_check) != 0 && !stream && seq_count != 1)) {
                reject(schema_error::unsupported_type, type);
            } else {
                const auto node = build_record(seed, stream ? field_mode::stream
                    : (seq_count == 1 ? field_mode::automatic_record : field_mode::explicit_record), children);
                nodes[index] = node;
            }
            nodes[index]->inline_mapping = forced_inline;
            ancestors.pop_back();
            return index;
        }

        static consteval schema_node_storage finish_leaf(const schema_seed &seed, schema_extent extent,
                                                          const schema_leaf &leaf) {
            return {seed.type, leaf_wire_type(seed.type, extent, leaf.layout().fields()), extent, seed.binding,
                    schema_kind::leaf, {.leaf = leaf}};
        }

        consteval schema_node_storage build_forced_leaf(const schema_seed &seed) {
            const auto type = seed.type;
            std::vector<object_field> fields;
            collect_object_fields(type, {}, 0, fields, path, seed.validation);
            const auto extent = schema_extent::fixed(std::meta::size_of(type));
            return finish_leaf(seed, extent, {leaf_strategy::forced, leaf_representation::forced,
                make_object_layout(type, std::move(fields), false)});
        }

        [[noreturn]] consteval void reject_automatic_leaf(std::meta::info type) {
            constexpr auto reason = schema_error::invalid_object_representation;
            if (!std::meta::is_trivially_copyable_type(type)) {
                reject(reason, type, "automatic tag::leaf requires a trivially copyable type");
            }
            if (has_transient_members(type) && !transient_layout_supports_segmented_copy(type)) {
                reject(reason, type, "automatic tag::leaf requires a transient layout that supports segmented copy");
            }
            if (!leaf_allows_object_representation(type)) {
                reject(reason, type, "tag::leaf requires std::has_unique_object_representations_v<T>; "
                    "set .allow_non_unique_presentation = true to opt in to non-unique object representations");
            }
            reject(reason, type, "tag::leaf requires a compatible leaf_codec");
        }

        consteval schema_node_storage build_codec_leaf(const schema_seed &seed, const schema_type_facts &facts) {
            const auto type = seed.type;
            const auto extent = schema_extent::fixed(facts.codec_extent);
            const auto strategy = facts.strategy == leaf_strategy::scalar_checked && seed.validation == validation_mode::unchecked
                ? leaf_strategy::scalar_unchecked : facts.strategy;
            if (!facts.has_object_layout) {
                return finish_leaf(seed, extent, {strategy, facts.representation});
            }
            // Validate before entering the codec's separate constant initializer, so
            // inspection can report the enclosing schema path.
            std::vector<object_field> fields;
            collect_object_fields(type, {}, 0, fields, path, seed.validation);
            const auto layout = strategy == leaf_strategy::object
                ? make_object_layout(type, std::move(fields), facts.representation == leaf_representation::segmented)
                : *std::meta::extract<const object_layout *>(std::meta::substitute(^^codec_object_layout, {type}));
            return finish_leaf(seed, extent, {strategy, facts.representation, layout});
        }

        consteval schema_node_storage build_container(const schema_seed &seed, const schema_type_facts &facts, std::vector<std::size_t> &children) {
            const auto type = seed.type;
            const auto shape = facts.group == 1 ? schema_container_shape::sequence : schema_container_shape::map;
            if (facts.count != std::dynamic_extent
                && facts.count > entry_metadata::max_mapping_count / facts.group) {
                reject(schema_error::mapping_limit, type);
            }
            children.push_back(append(schema_seed{
                .type = std::meta::remove_cvref(facts.first),
                .binding = {.kind = schema_binding_kind::slot, .slot = {
                    facts.group == 1 ? schema_slot_role::element : schema_slot_role::key, facts.first}},
                .validation = seed.validation,
            }));
            if ((facts.count == std::dynamic_extent || facts.group == 2)
                && !schema_default_initializable(facts.first)) {
                reject(schema_error::missing_default_constructor, facts.first);
            }
            if (facts.group == 2) {
                children.push_back(append(schema_seed{
                    .type = std::meta::remove_cvref(facts.second),
                    .binding = {.kind = schema_binding_kind::slot, .slot = {schema_slot_role::mapped, facts.second}},
                    .validation = seed.validation,
                }));
                if (!schema_default_initializable(facts.second)) {
                    reject(schema_error::missing_default_constructor, facts.second);
                }
            }
            auto stride = schema_extent::fixed(0);
            for (auto child: children) {
                stride += nodes[child]->encode_extent;
            }
            const auto payload = facts.count == std::dynamic_extent
                ? schema_extent::dynamic() : stride.scaled_by(facts.count);
            const auto extent = schema_extent::fixed(entry_head_wire_size) + payload;
            const schema_container container{edges.size(), facts.count, shape, seed.duplicates};
            const auto hash = composite_wire_type(container.wire_kind(), children);
            append_edges(children);
            return {type, hash, extent, seed.binding, schema_kind::container, {.container = container}};
        }

        consteval schema_node_storage build_record(const schema_seed &seed, field_mode mode, std::vector<std::size_t> &children) {
            const auto type = seed.type;
            const bool stream = mode == field_mode::stream;
            field_build_context context{.owner = type, .mode = mode};
            for (auto entity: reflected_entities(type)) {
                auto child = field(entity, context);
                if (child) {
                    child->validation = validation_for(entity, seed.validation);
                    children.push_back(append(*child, annotation_count(entity, ^^tag::auto_stream) == 1));
                }
            }
            if (!stream) {
                if (children.size() > entry_metadata::max_mapping_count) {
                    reject(schema_error::mapping_limit, type);
                }
                std::ranges::sort(children, std::ranges::less{},
                    [&](std::size_t child) { return nodes[child]->binding.record.id; });
                for (std::size_t i = 1; i < children.size(); ++i) {
                    const auto &previous = nodes[children[i - 1]]->binding.record;
                    const auto &current = nodes[children[i]]->binding.record;
                    if (previous.id == current.id) {
                        reject(schema_error::duplicate_field_id, current.field.entity,
                            std::string("field id=") + number_to_string(current.id)
                            + " also used by " + std::string(std::meta::display_string_of(previous.field.entity))
                            + "; assign distinct field IDs");
                    }
                }
            }
            auto encode_extent = schema_extent::fixed(entry_head_wire_size);
            for (auto child: children) {
                encode_extent += nodes[child]->encode_extent;
            }
            if (stream) {
                auto decode_extent = schema_extent::fixed(entry_head_wire_size);
                for (auto child: children) {
                    decode_extent += nodes[child]->decode_extent();
                }
                const auto hash = composite_wire_type(entry_kind::stream, children);
                return {type, hash, encode_extent, seed.binding, schema_kind::stream,
                        {.stream = {append_edges(children), decode_extent}}};
            }
            return {type, composite_wire_type(entry_kind::record, children), encode_extent, seed.binding,
                    schema_kind::record, {.record = append_edges(children)}};
        }

        consteval schema_edges append_edges(std::span<const std::size_t> children) {
            const schema_edges range{edges.size(), children.size()};
            edges.insert(edges.end(), children.begin(), children.end());
            return range;
        }

        consteval type_hash_type composite_wire_type(entry_kind kind, std::span<const std::size_t> children) {
            auto hash = hash_integer(fnv1a32_offset, std::to_underlying(kind));
            if (kind != entry_kind::record) {
                for (auto child: children) {
                    const auto &node = *nodes[child];
                    hash = hash_integer(hash, node.wire_type);
                    if (kind == entry_kind::stream) {
                        const auto entity = node.binding.stream.entity;
                        hash = hash_string(hash, std::meta::is_base(entity)
                            ? std::meta::display_string_of(std::meta::type_of(entity))
                            : std::meta::identifier_of(entity));
                    }
                }
            }
            return hash;
        }

        consteval void validate_field_annotations(const field_metadata &metadata, const field_build_context &context) {
            const auto &facts = metadata.facts;
            const auto entity = metadata.entity;
            const bool stream = context.stream();
            const bool automatic = context.automatic();
            const bool serialized = automatic || facts.fields == 1;
            if (annotation_count(entity, ^^tag::inline_mapping) != 0 && (!serialized || facts.transient != 0)) {
                reject(schema_error::conflicting_annotations, entity, "inline_mapping requires a serialized field");
            }

            if (facts.validation_bypasses > 1
                || (facts.validation_bypasses != 0 && (!serialized || facts.transient != 0))) {
                reject(schema_error::conflicting_annotations, entity);
            }

            if (facts.fallbacks > 1 || (facts.fallbacks != 0 && (stream || !serialized || facts.transient != 0))) {
                reject(schema_error::conflicting_annotations, entity);
            }
            if (facts.fallbacks == 1 && !fallback_valid(entity)) {
                reject(schema_error::invalid_fallback, entity,
                    fallback_annotation_type(entity) == ^^tag::fallback<defaults::automatic>
                        ? "automatic fallback requires Owner{} and the field snapshot to be a constant expression; "
                          "use an explicit value/provider for unsupported defaults"
                        : "fallback policy cannot be applied to the field type");
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

        consteval id_type field_id(const field_metadata &metadata, field_build_context &context) {
            if (metadata.facts.fields == 1) {
                context.next_id = std::meta::extract<tag::field>(metadata.annotation(^^tag::field)).id;
            }
            if (context.next_id > std::numeric_limits<id_type>::max()) {
                reject(schema_error::field_id_overflow, metadata.entity);
            }
            return static_cast<id_type>(context.next_id++);
        }

        consteval void apply_conversion(schema_seed &seed, schema_field_binding &field, const field_metadata &metadata) {
            const auto &facts = metadata.facts;
            const auto entity = metadata.entity;
            const auto conversion = std::meta::extract<conversion_tag>(metadata.annotation(^^conversion_tag));
            const auto storage = conversion.target_type;
            if (!std::meta::is_type(storage) || std::meta::is_reference_type(facts.declared)
                || std::meta::is_const_type(facts.declared) || std::meta::is_volatile_type(facts.declared)
                || !std::meta::is_same_type(storage, std::meta::remove_cvref(storage))) {
                reject(schema_error::invalid_conversion, entity);
            }
            if (!schema_conversion_valid(metadata.memory, storage)
                || (conversion.handler_id == nullptr
                    && !std::meta::extract<bool>(std::meta::substitute(^^schema_cast_valid_v, {metadata.memory, storage})))) {
                reject(schema_error::invalid_conversion, entity);
            }
            seed.type = storage;
            field.conversion = conversion.handler_id == nullptr ? schema_conversion_kind::cast : schema_conversion_kind::handler;
            field.handler_id = conversion.handler_id;
        }

        consteval std::span<const id_type> accepted_ids(std::meta::info entity, id_type primary_id) {
            std::vector<id_type> ids;
            for (auto annotation: std::meta::annotations_of_with_type(entity, ^^tag::accept_id)) {
                const auto id = std::meta::extract<tag::accept_id>(annotation).id;
                if (id == primary_id || std::ranges::find(ids, id) != ids.end()) {
                    reject(schema_error::conflicting_annotations, entity, "accept_id must be distinct from the primary ID and other aliases");
                }
                ids.push_back(id);
            }
            return ids.empty() ? std::span<const id_type>{} : std::define_static_array(ids);
        }

        consteval std::optional<schema_seed> field(std::meta::info entity, field_build_context &context) {
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

            schema_seed seed{.type = metadata.memory};
            schema_field_binding binding{entity};
            const auto fallback = facts.fallbacks == 1 ? resolved_fallback_type(entity) : std::meta::info{};
            const auto id = context.stream() ? id_type{} : field_id(metadata, context);
            if (facts.unsupported || std::meta::is_const_type(facts.declared)
                || std::meta::is_volatile_type(facts.declared)) {
                reject(schema_error::unsupported_entity, entity);
            }

            if (facts.conversions == 1) {
                apply_conversion(seed, binding, metadata);
            } else if (forced) {
                seed.forced = true;
            } else if (std::meta::is_reference_type(facts.declared) || std::meta::is_pointer_type(metadata.memory)) {
                reject(schema_error::unsupported_entity, entity);
            }
            if (facts.containers == 1) {
                if (!schema_facts(seed.type).container) {
                    reject(schema_error::conflicting_annotations, entity);
                }
                seed.duplicates = std::meta::extract<tag::container>(metadata.annotation(^^tag::container)).duplicates == duplicate_policy::reject
                    ? schema_duplicates::reject : schema_duplicates::deduplicate;
            }
            const auto aliases = context.stream() ? std::span<const id_type>{} : accepted_ids(entity, id);
            seed.binding = context.stream()
                ? schema_binding{.kind = schema_binding_kind::stream_field, .stream = binding}
                : schema_binding{.kind = schema_binding_kind::record_field,
                    .record = {binding, id, fallback, aliases.data(), aliases.size()}};
            return seed;
        }
    };

    consteval schema_tree build_schema(std::meta::info type, bool stream = false) {
        type = recursively_dealias(std::meta::remove_cvref(type));
        schema_builder builder;
        builder.nodes.emplace_back(std::nullopt);
        const auto payload = builder.append(schema_seed{.type = type}, stream);
        const auto &value = *builder.nodes[payload];
        const auto kind = value.wire_kind();
        const auto header = schema_extent::fixed(kind == entry_kind::leaf ? entry_head_wire_size : 0);
        builder.path.push_back(type);
        try {
            builder.nodes[schema_tree::root_index] = schema_node_storage{
                type, value.wire_type, header + value.encode_extent, {}, schema_kind::root,
                {.root = {builder.edges.size(), kind}}};
        } catch (const extent_overflow &failure) {
            builder.reject(schema_error::extent_overflow, {}, failure.what());
        }
        builder.edges.push_back(payload);
        const auto complete = builder.nodes | std::views::transform(
            [](const auto &node) -> const schema_node_storage & { return *node; });
        return {std::define_static_array(complete), std::define_static_array(builder.edges)};
    }
}
