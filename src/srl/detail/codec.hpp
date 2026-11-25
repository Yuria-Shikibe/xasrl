#pragma once

#include <array>
#include <bit>
#include <inplace_vector>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <memory_resource>
#include <meta>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../cond_value.hpp"
#include "../context.hpp"
#include "../number_to_string.hpp"
#include "../schema.hpp"
#include "../storage/encoding_recorder.hpp"
#include "codec_support.hpp"
#include "wire_document.hpp"

#if defined(_MSC_VER)
#define SRL_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define SRL_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace xasrl::detail {
    template<codec_status Status>
    constexpr Status locate_error(Status failure, std::optional<id_type> id, szt offset = 0) noexcept {
        return failure.set_id(id).rebase(offset);
    }

    template<std::size_t Extent, typename Byte, std::size_t SourceExtent>
    auto payload_span(std::span<Byte, SourceExtent> bytes, szt offset, szt size) {
        if constexpr (Extent == std::dynamic_extent) {
            return bytes.subspan(offset, size);
        } else {
            return bytes.subspan(offset).template first<Extent>();
        }
    }

    template<const schema_tree &Tree, std::size_t Node>
    void finish_header(std::span<std::byte, entry_head_wire_size> header, bool only,
                       szt size, std::size_t count) {
        static constexpr auto node = Tree[Node];
        static constexpr auto wire_kind = node.wire_kind();
        static constexpr auto wire_type = node.wire_type();
        static constexpr auto embedded = mapping_layout_for<Tree>.extent(Node, false);
        if constexpr (embedded.is_dynamic()) {
            write_trivial(header, entry_head{size, {count, wire_kind}, wire_type});
        } else {
            static constexpr auto constant_count = [] consteval {
                if constexpr (node.kind() == schema_kind::container) {
                    return node.container().element_count * node.container().slot_count();
                } else if constexpr (node.kind() == schema_kind::record) {
                    return Tree.child_indices(Node).size();
                } else {
                    return std::size_t{0};
                }
            }();
            static constexpr auto local = mapping_layout_for<Tree>.extent(Node, true);
            static constexpr auto embedded_bytes = std::bit_cast<std::array<std::byte, entry_head_wire_size>>(
                entry_head{embedded.value, {constant_count, wire_kind}, wire_type});
            static constexpr auto local_bytes = std::bit_cast<std::array<std::byte, entry_head_wire_size>>(
                entry_head{local.value, {constant_count, wire_kind}, wire_type});
            const auto &bytes = only ? local_bytes : embedded_bytes;
            std::memcpy(header.data(), bytes.data(), bytes.size());
        }
    }
    consteval bool encoding_needs_cache(const schema_tree &tree, std::size_t node) {
        const auto &descriptor = tree[node];
        if (descriptor.kind() == schema_kind::container && descriptor.container().element_count == 0) { return false; }
        for (auto child: tree.child_indices(node)) {
            if (tree[child].converted() || encoding_needs_cache(tree, child)) {
                return true;
            }
        }
        return false;
    }

    consteval bool encoding_uses_recorder(const schema_tree &tree, std::size_t node) {
        const auto &descriptor = tree[node];
        if (descriptor.kind() == schema_kind::container && descriptor.container().element_count == 0) { return false; }
        for (auto child: tree.child_indices(node)) {
            if ((descriptor.kind() == schema_kind::container && encoding_needs_cache(tree, child))
                || encoding_uses_recorder(tree, child)) { return true; }
        }
        return false;
    }

    template<const schema_tree &Tree, std::size_t Node>
    using node_value_t = [:Tree[Node].type():];

    template<const schema_tree &Tree, std::size_t Node>
    struct value_codec;

    template<const schema_tree &Tree, std::size_t Node>
        requires (Tree[Node].kind() == schema_kind::leaf)
    struct value_codec<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr schema_node descriptor = Tree[Node];

        static constexpr object_layout object = descriptor.leaf().layout();

        struct encode_plan {
            static constexpr auto size = descriptor.encode_extent().value;

            using prepare_result = always_success;
            prepare_result prepare(const value_type &, const serialization_context &, encoding_session *, szt &measured, bool) {
                measured = size;
                return {};
            }

            auto encode(encoding_output &out, const value_type &value, encoding_cursor &) const {
                auto destination = out.take<size>();
                if constexpr (descriptor.leaf().strategy == leaf_strategy::forced) {
                    return detail::encode_object_bytes<object>(value, destination);
                } else {
                    return leaf_codec<value_type>::encode(value, destination);
                }
            }

            using encode_result = decltype(std::declval<const encode_plan &>().encode(
                std::declval<encoding_output &>(),
                std::declval<const value_type &>(), std::declval<encoding_cursor &>()));
        };

        static auto decode_payload(std::span<const std::byte, descriptor.encode_extent().value> source, value_type &out) {
            if constexpr (descriptor.leaf().strategy == leaf_strategy::forced || descriptor.leaf().strategy == leaf_strategy::object) {
                return detail::decode_object_bytes<object>(source, out);
            } else if constexpr (descriptor.leaf().scalar()) {
                return scalar_codec<value_type>::template decode_with_validation<descriptor.leaf().strategy == leaf_strategy::scalar_checked
                    ? validation_mode::checked : validation_mode::unchecked>(source, out);
            } else {
                return leaf_codec<value_type>::decode(source, out);
            }
        }

        using payload_decode_result = decltype(value_codec::decode_payload(
            std::declval<std::span<const std::byte, descriptor.encode_extent().value>>(), std::declval<value_type &>()));

        static constexpr bool bulk_decodable = [] consteval {
            if constexpr (!std::same_as<payload_decode_result, always_success>
                          || !std::is_trivially_copyable_v<value_type>) {
                return false;
            } else if constexpr (descriptor.leaf().scalar()) {
                return std::same_as<typename scalar_codec<value_type>::wire_type, value_type>;
            } else if constexpr (descriptor.leaf().strategy == leaf_strategy::object
                                 || descriptor.leaf().strategy == leaf_strategy::forced) {
                return object.payload_size == sizeof(value_type) && descriptor.decode_extent().value == sizeof(value_type);
            } else {
                return false;
            }
        }();

        template<std::size_t Extent>
        [[nodiscard]] static combined_status<status_type<Extent == descriptor.encode_extent().value>, payload_decode_result> decode(
            std::span<const std::byte, Extent> source, value_type &out,
            const serialization_context &, const wire_document & = {}) {
            // Validate untrusted lengths before forming the fixed-size codec span.
            constexpr auto extent = descriptor.decode_extent().value;
            if constexpr (Extent != std::dynamic_extent && Extent != extent) {
                return fail(error_code::invalid_payload_shape);
            } else {
                if constexpr (Extent == std::dynamic_extent) {
                    if (source.size() != extent) {
                        return fail(error_code::invalid_payload_shape);
                    }
                }
                auto status = value_codec::decode_payload(source.template first<extent>(), out);
                if constexpr (std::same_as<decltype(status), error>) {
                    if (status) { return status; }
                }
                return {};
            }
        }
    };

    struct no_conversion {};

    template<const schema_tree &Tree, std::size_t Node>
    struct field_codec {
        static constexpr schema_node descriptor = Tree[Node];

        using memory_type = [:descriptor.field().memory_type():];
        using storage_type = node_value_t<Tree, Node>;

        struct encode_plan {
            SRL_NO_UNIQUE_ADDRESS [:descriptor.converted() ? ^^std::optional<storage_type> : ^^no_conversion:] storage;
            SRL_NO_UNIQUE_ADDRESS value_codec<Tree, Node>::encode_plan payload;

            using prepare_result = combined_status<
                status_type<descriptor.field().conversion != schema_conversion_kind::handler>, typename decltype(payload)::prepare_result>;
            using encode_result = decltype(payload)::encode_result;

            template<typename T>
            prepare_result prepare(const T &object, const serialization_context &context, encoding_session *records, szt &measured, bool only) {
                const auto &mem = detail::entity_reference<descriptor.field().entity, (descriptor.field().is_base() ? reflected_entity_kind::base : reflected_entity_kind::member)>(object);
                if constexpr (descriptor.converted()) {
                    if constexpr (descriptor.field().conversion != schema_conversion_kind::handler) {
                        storage.emplace(static_cast<storage_type>(mem));
                    } else {
                        constexpr auto handler_id = descriptor.field().handler_id;
                        auto &handler = context.template get_handler<memory_type, storage_type>(handler_id);
                        storage.emplace();
                        if (auto status = handler.mem2srl(std::addressof(mem), std::addressof(*storage)); status) {
                            return status;
                        }
                    }
                    return payload.prepare(*storage, context, records, measured, only);
                } else {
                    return payload.prepare(mem, context, records, measured, only);
                }
            }

            template<typename T>
            encode_result encode(encoding_output &out, const T &object, encoding_cursor &records) const {
                if constexpr (descriptor.converted()) {
                    return payload.encode(out, *storage, records);
                } else {
                    return payload.encode(out, detail::entity_reference<descriptor.field().entity, (descriptor.field().is_base() ? reflected_entity_kind::base : reflected_entity_kind::member)>(object), records);
                }
            }
        };

        template<typename T>
        [[nodiscard]] static error decode(
            const field_view &field, T &out, const serialization_context &context, const wire_document &doc = {}) {
            if (field.wire_type != descriptor.wire_type()) {
                return fail(error_code::type_mismatch, detail::diagnostic_id(descriptor), field.offset);
            }
            auto &mem = detail::entity_reference<descriptor.field().entity, (descriptor.field().is_base() ? reflected_entity_kind::base : reflected_entity_kind::member)>(out);
            if constexpr (descriptor.converted()) {
                storage_type storage{};
                if constexpr (descriptor.field().conversion != schema_conversion_kind::handler) {
                    if (auto status = value_codec<Tree, Node>::decode(field.bytes, storage, context, doc); status) {
                        return detail::locate_error(status, detail::diagnostic_id(descriptor), field.offset);
                    }
                    mem = static_cast<memory_type>(std::move(storage));
                } else {
                    constexpr auto handler_id = descriptor.field().handler_id;
                    auto &handler = context.template get_handler<memory_type, storage_type>(handler_id);
                    if (auto status = value_codec<Tree, Node>::decode(field.bytes, storage, context, doc); status) {
                        return detail::locate_error(status, detail::diagnostic_id(descriptor), field.offset);
                    }
                    if (auto status = handler.srl2mem(std::addressof(mem), std::addressof(storage)); status) {
                        return detail::locate_error(status, detail::diagnostic_id(descriptor), field.offset);
                    }
                }
            } else {
                if (auto status = value_codec<Tree, Node>::decode(field.bytes, mem, context, doc); status) {
                    return detail::locate_error(status, detail::diagnostic_id(descriptor), field.offset);
                }
            }
            return {};
        }
    };

    template<const schema_tree &Tree, std::size_t Node>
    using field_encode_plan = field_codec<Tree, Node>::encode_plan;

    template<const auto &Members, bool Prepare>
    consteval bool plans_always_succeed() {
        template for (constexpr auto member: Members) {
            using plan = [:std::meta::type_of(member):];
            if constexpr (Prepare) {
                if constexpr (!std::same_as<typename plan::prepare_result, always_success>) { return false; }
            } else {
                if constexpr (!std::same_as<typename plan::encode_result, always_success>) { return false; }
            }
        }
        return true;
    }

    consteval std::meta::info define_plan_members(
        std::meta::info target, std::meta::info plan_template, std::meta::info tree_reference,
        const schema_tree &tree, std::size_t node) {
        std::vector<std::meta::info> members;
        for (auto child: tree.child_indices(node)) {
            const auto type = std::meta::substitute(plan_template,
                {tree_reference, std::meta::reflect_constant(child)});
            const auto ordinal = number_to_string(members.size());
            std::string name = "field_";
            name.append(ordinal.view());
            members.push_back(std::meta::data_member_spec(type, {.name = name, .no_unique_address = true}));
        }
        return std::meta::define_aggregate(target, members);
    }

    template<const schema_tree &Tree, std::size_t Node>
        requires (Tree[Node].kind() == schema_kind::record || Tree[Node].kind() == schema_kind::stream)
    struct value_codec<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr schema_node descriptor = Tree[Node];

        struct encode_plan {
            struct fields_type;
            consteval {
                define_plan_members(^^fields_type, ^^field_encode_plan, std::meta::reflect_object(Tree), Tree, Node);
            }
            static constexpr std::span<const std::meta::info> members = std::define_static_array(
                std::meta::nonstatic_data_members_of(^^fields_type, std::meta::access_context::current()));

            SRL_NO_UNIQUE_ADDRESS fields_type fields;

            using prepare_result = status_type<descriptor.encode_extent().value != std::dynamic_extent
                && plans_always_succeed<members, true>()>;
            using encode_result = status_type<plans_always_succeed<members, false>()>;
            prepare_result prepare(const value_type &value, const serialization_context &context, encoding_session *records, szt &measured, bool only) {
                static constexpr auto embedded_extent = mapping_layout_for<Tree>.extent(Node, false);
                static constexpr auto local_extent = mapping_layout_for<Tree>.extent(Node, true);
                const auto extent = only ? local_extent : embedded_extent;
                static constexpr auto extra = mapping_layout_for<Tree>.nodes[Node].prefix.size();
                measured = embedded_extent.is_dynamic() ? entry_head_wire_size + (only ? 0 : extra) : extent.value;
                template for (constexpr auto ordinal: std::views::iota(std::size_t{}, members.size())) {
                    constexpr auto member = members[ordinal];
                    static constexpr schema_node field_node = Tree[Node, ordinal];
                    auto &field = fields.[:member:];
                    szt field_size{};
                    if constexpr (auto status = field.prepare(value, context, records, field_size, only); std::same_as<decltype(status), error>) {
                        if (status) { return detail::locate_error(status, detail::diagnostic_id(field_node)); }
                    }
                    if constexpr (descriptor.encode_extent().value == std::dynamic_extent) {
                        if (auto status = detail::append_encoded_size(measured, field_size, 0, field_node.field_id().value_or(0)); status) {
                            return status;
                        }
                    }
                }
                return {};
            }

            encode_result encode(encoding_output &out, const value_type &value, encoding_cursor &records) const {
                constexpr auto count = descriptor.kind() == schema_kind::stream ? 0 : members.size();
                const auto start = out.position();
                auto header = out.take<entry_head_wire_size>();
                static constexpr auto prefix = mapping_layout_for<Tree>.nodes[Node].prefix;
                if (!out.schema_only()) { out.copy(prefix); }
                template for (constexpr auto ordinal: std::views::iota(std::size_t{}, members.size())) {
                    constexpr auto member = members[ordinal];
                    static constexpr schema_node field_node = Tree[Node, ordinal];
                    const auto &field = fields.[:member:];
                    const auto field_start = out.position();
                    if constexpr (auto status = field.encode(out, value, records); std::same_as<decltype(status), error>) {
                        if (status) { return detail::locate_error(status, detail::diagnostic_id(field_node), field_start - start); }
                    }
                }
                finish_header<Tree, Node>(header, out.schema_only(), out.position() - start, count);
                return {};
            }
        };

        [[nodiscard]] static error decode(
            std::span<const std::byte> source, value_type &out,
            const serialization_context &context, const wire_document &doc) {
            auto entry = defined_entry::parse(source, doc, mapping_layout_for<Tree>.nodes[Node].definition);
            return entry ? value_codec::decode_entry(*entry, out, context, doc) : entry.error();
        }

        [[nodiscard]] static error decode_entry(
            const defined_entry &entry, value_type &out,
            const serialization_context &context, const wire_document &doc) {
            if (auto status = validate_entry_type(entry, descriptor.wire_kind(), descriptor.wire_type()); status) {
                return status;
            }
            if constexpr (descriptor.kind() == schema_kind::stream) {
                if (entry.count() != Tree.child_indices(Node).size()) {
                    return fail(error_code::invalid_entry_classification, std::nullopt, 8);
                }
            }
            defined_entry::cursor cursor;
            template for (constexpr auto child: Tree.child_indices(Node)) {
                static constexpr schema_node field_node = Tree[child];
                auto field = [&]() -> std::expected<field_view, error> {
                    if constexpr (descriptor.kind() != schema_kind::stream) {
                        constexpr auto id = field_node.record_field().id;
                        return entry.find(id, cursor);
                    } else {
                        return entry.next(cursor);
                    }
                }();
                if (!field) {
                    if constexpr (descriptor.kind() != schema_kind::stream && field_node.record_field().fallback_type().has_value()) {
                        if (field.error().code == error_code::missing_field) {
                            using fallback = [:*field_node.record_field().fallback_type():];
                            constexpr auto kind = field_node.field().is_base()
                                ? reflected_entity_kind::base : reflected_entity_kind::member;
                            fallback::apply(entity_reference<field_node.field().entity, kind>(out), context);
                            continue;
                        }
                    }
                    return field.error();
                }
                if (auto status = field_codec<Tree, child>::decode(*field, out, context, doc); status) {
                    return status;
                }
            }
            return entry.finish(cursor);
        }
    };

    template<std::size_t Slot, typename Expected, typename Value, typename Visitor>
    bool visit_container_slot(Value &&value, Visitor &&visitor) {
        if constexpr (std::meta::is_same_type(std::meta::remove_cvref(^^Value), ^^Expected)) {
            return visitor.template operator()<Slot>(value);
        } else {
            Expected converted(std::forward<Value>(value));
            return visitor.template operator()<Slot>(converted);
        }
    }

    template<container_serializable T, typename Visitor>
    void for_each_container_slot(const T &value, Visitor &&visitor) {
        using traits = container_traits<T>;
        for (const auto &element: detail::container_range(value)) {
            if constexpr (traits::mapping_group_size == 1) {
                if (!detail::visit_container_slot<0, typename traits::element_type>(element, visitor)) { return; }
            } else {
                if constexpr (requires { traits::key(element); traits::mapped(element); }) {
                    if (!detail::visit_container_slot<0, typename traits::key_type>(traits::key(element), visitor)) { return; }
                    if (!detail::visit_container_slot<1, typename traits::mapped_type>(traits::mapped(element), visitor)) { return; }
                } else {
                    if (!detail::visit_container_slot<0, typename traits::key_type>(element.first, visitor)) { return; }
                    if (!detail::visit_container_slot<1, typename traits::mapped_type>(element.second, visitor)) { return; }
                }
            }
        }
    }

    template<const schema_tree &Tree, std::size_t Node>
    struct container_slot_plan {
        using plan_type = typename value_codec<Tree, Node>::encode_plan;
        static constexpr bool cached = encoding_needs_cache(Tree, Node);
        using prepare_result = typename plan_type::prepare_result;
        using encode_result = typename plan_type::encode_result;

        static decltype(auto) read(encoding_cursor &records) {
            if constexpr (cached) {
                return records.read<plan_type>();
            } else {
                return plan_type{};
            }
        }
    };

    template<typename T>
    inline constexpr std::size_t container_capacity = std::dynamic_extent;

    template<typename T, std::size_t Capacity>
    inline constexpr std::size_t container_capacity<std::inplace_vector<T, Capacity>> = Capacity;

    template<typename T, typename Allocator>
    inline constexpr bool ordinary_element_allocator = std::same_as<Allocator, std::allocator<T>>
        || (std::same_as<Allocator, std::pmr::polymorphic_allocator<T>> && !std::uses_allocator_v<T, Allocator>);

    template<typename T>
    inline constexpr bool bulk_resizable_sequence = false;

    template<typename T, typename Allocator>
    inline constexpr bool bulk_resizable_sequence<std::vector<T, Allocator>> = ordinary_element_allocator<T, Allocator>;

    template<typename T, typename Allocator>
    inline constexpr bool bulk_resizable_sequence<std::basic_string<T, std::char_traits<T>, Allocator>> =
        ordinary_element_allocator<T, Allocator>;

    template<typename T, std::size_t Capacity>
    inline constexpr bool bulk_resizable_sequence<std::inplace_vector<T, Capacity>> = true;

    template<typename T>
    using mutable_container_range = decltype(detail::container_range(std::declval<T &>()));

    // Dynamic containers may replace insertion with resize only when neither
    // the container adapter nor the allocator customizes element construction.
    template<typename T, typename Element>
    concept bulk_sequence_destination = std::ranges::contiguous_range<mutable_container_range<T>>
        && std::same_as<std::ranges::range_reference_t<mutable_container_range<T>>, Element &>
        && (detail::fixed_container<T> || (bulk_resizable_sequence<T>
            && std::same_as<typename T::value_type, Element>
            && std::is_nothrow_default_constructible_v<Element>
            && std::is_trivially_constructible_v<Element, Element &&>
            && !requires(T &out) { container_traits<T>::range(out); }
            && !requires(T &out) { container_traits<T>::clear(out); }
            && !requires(T &out, std::size_t size) { container_traits<T>::reserve(out, size); }
            && !requires(T &out, Element &&element) { container_traits<T>::insert(out, std::move(element)); }));

    template<const schema_tree &Tree, std::size_t Node>
        requires (Tree[Node].kind() == schema_kind::container)
    struct value_codec<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr schema_node descriptor = Tree[Node];
        static constexpr auto group_size = descriptor.container().slot_count();
        static constexpr auto fixed_count = descriptor.container().element_count;

        template<std::size_t Slot>
        using slot_codec = value_codec<Tree, Tree.child_indices(Node)[Slot]>;

        struct fixed_group_layout {
            szt stride{};
            std::array<definition_row, group_size> rows{};
            std::array<szt, group_size> offsets{};
        };

        static constexpr auto fixed_group = [] consteval {
            fixed_group_layout layout;
            for (std::size_t i = 0; i != group_size; ++i) {
                const auto child = Tree[Node, i];
                if (child.kind() != schema_kind::leaf || child.decode_extent().value > maximum_entry_size - layout.stride) {
                    layout.stride = std::dynamic_extent;
                    break;
                }
                const auto width = child.decode_extent().value;
                layout.rows[i] = {static_cast<id_type>(i), child.wire_type(), width};
                layout.offsets[i] = layout.stride;
                layout.stride += width;
            }
            return layout;
        }();

        struct encode_plan {
            template<std::size_t Slot>
            using slot_plan = container_slot_plan<Tree, Tree.child_indices(Node)[Slot]>;

            template<std::size_t... Slots>
            static auto slot_results(std::index_sequence<Slots...>) -> std::pair<
                combined_status<typename slot_plan<Slots>::prepare_result...>,
                combined_status<typename slot_plan<Slots>::encode_result...>>;

            using results = decltype(slot_results(std::make_index_sequence<
                descriptor.container().element_count == 0 ? 0 : descriptor.container().slot_count()>{}));
            using prepare_result = status_type<descriptor.container().element_count == 0
                || (descriptor.container().element_count != std::dynamic_extent && descriptor.encode_extent().value != std::dynamic_extent
                    && std::same_as<typename results::first_type, always_success>)>;
            using encode_result = typename results::second_type;
            static constexpr bool fixed_slot_extents = [] consteval {
                for (auto child: Tree.child_indices(Node)) {
                    if (Tree[child].encode_extent().is_dynamic()) { return false; }
                }
                return true;
            }();

            static constexpr bool has_slot_conversions = [] consteval {
                for (auto child: Tree.child_indices(Node)) {
                    if (encoding_needs_cache(Tree, child)) { return true; }
                }
                return false;
            }();

            prepare_result prepare(const value_type &value, const serialization_context &context, encoding_session *records, szt &measured, bool only) {
                constexpr auto group_size = descriptor.container().slot_count();
                constexpr auto element_count = descriptor.container().element_count;
                static constexpr auto embedded_extent = mapping_layout_for<Tree>.extent(Node, false);
                static constexpr auto local_extent = mapping_layout_for<Tree>.extent(Node, true);
                const auto extent = only ? local_extent : embedded_extent;
                static constexpr auto extra = mapping_layout_for<Tree>.nodes[Node].prefix.size();
                measured = embedded_extent.is_dynamic() ? entry_head_wire_size + (only ? 0 : extra) : extent.value;
                if constexpr (element_count == 0 || (!embedded_extent.is_dynamic() && !has_slot_conversions)) {
                    return {};
                } else if constexpr (fixed_slot_extents && !has_slot_conversions
                                     && std::ranges::sized_range<decltype(detail::container_range(value))>) {
                    static constexpr auto extents = [](bool local) consteval {
                        std::array<szt, descriptor.container().slot_count()> result;
                        for (std::size_t i = 0; i != result.size(); ++i) {
                            result[i] = mapping_layout_for<Tree>.extent(Tree.child_indices(Node)[i], local).value;
                        }
                        return result;
                    };
                    const auto count = std::ranges::size(detail::container_range(value));
                    static constexpr auto local_slots = extents(true);
                    static constexpr auto embedded_slots = extents(false);
                    auto total = measure_fixed_slots(count, only ? local_slots : embedded_slots, measured);
                    if (!total) { return total.error(); }
                    measured = *total;
                    return {};
                } else {
                    prepare_result status{};
                    id_type slot_index{};
                    if constexpr (element_count == std::dynamic_extent
                                  && std::ranges::sized_range<decltype(detail::container_range(value))>) {
                        const auto count = std::ranges::size(detail::container_range(value));
                        if (count > entry_metadata::max_mapping_count / group_size) {
                            return fail(error_code::too_many_mappings, entry_metadata::max_mapping_count);
                        }
                    }
                    detail::for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) -> bool {
                        if constexpr (element_count == std::dynamic_extent) {
                            if (slot_index == entry_metadata::max_mapping_count) {
                                status = fail(error_code::too_many_mappings, slot_index);
                                return false;
                            }
                        }
                        using slot_state = slot_plan<Slot>;
                        using plan_type = typename slot_state::plan_type;
                        szt plan_size{};
                        auto prepare_slot = [&](plan_type &plan) {
                            if constexpr (auto prepared = plan.prepare(slot, context, records, plan_size, only);
                                          std::same_as<decltype(prepared), error>) {
                                if (prepared) {
                                    status = detail::locate_error(prepared, slot_index);
                                    return false;
                                }
                            }
                            return true;
                        };
                        if constexpr (slot_state::cached) {
                            if (!prepare_slot(records->append<plan_type>())) { return false; }
                        } else {
                            plan_type plan;
                            if (!prepare_slot(plan)) { return false; }
                        }
                        if constexpr (embedded_extent.is_dynamic()) {
                            if (auto failure = append_encoded_size(measured, plan_size, 0, slot_index); failure) {
                                status = failure;
                                return false;
                            }
                        }
                        ++slot_index;
                        return true;
                    });
                    return status;
                }
            }

            encode_result encode(encoding_output &out, const value_type &value, encoding_cursor &records) const {
                constexpr auto element_count = descriptor.container().element_count;
                constexpr auto group_size = descriptor.container().slot_count();
                const auto count = [&] {
                    if constexpr (element_count != std::dynamic_extent) {
                        return element_count * group_size;
                    } else {
                        auto &&range = detail::container_range(value);
                        if constexpr (std::ranges::sized_range<decltype(range)>) {
                            return static_cast<std::size_t>(std::ranges::size(range)) * group_size;
                        } else {
                            return static_cast<std::size_t>(std::ranges::distance(range)) * group_size;
                        }
                    }
                }();
                const auto start = out.position();
                auto header = out.take<entry_head_wire_size>();
                static constexpr auto prefix = mapping_layout_for<Tree>.nodes[Node].prefix;
                if (!out.schema_only()) { out.copy(prefix); }
                std::size_t index{};
                encode_result status{};
                if constexpr (element_count != 0) {
                    detail::for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) -> bool {
                        const auto &plan = slot_plan<Slot>::read(records);
                        const auto slot_start = out.position();
                        if constexpr (auto encoded = plan.encode(out, slot, records); std::same_as<decltype(encoded), error>) {
                            if (encoded) {
                                status = locate_error(encoded, static_cast<id_type>(index), slot_start - start);
                                return false;
                            }
                        }
                        ++index;
                        return true;
                    });
                }
                if (!status) {
                    finish_header<Tree, Node>(header, out.schema_only(), out.position() - start, count);
                }
                return status;
            }
        };

        [[nodiscard]] static error decode(
            std::span<const std::byte> source, value_type &out,
            const serialization_context &context, const wire_document &doc) {
            auto entry = defined_entry::parse(source, doc, mapping_layout_for<Tree>.nodes[Node].definition);
            return entry ? value_codec::decode_entry(*entry, out, context, doc) : entry.error();
        }

        [[nodiscard]] static error decode_entry(
            const defined_entry &entry, value_type &out,
            const serialization_context &context, const wire_document &doc) {
            using T = value_type;
            if (auto status = validate_entry_type(entry, descriptor.wire_kind(), descriptor.wire_type()); status) {
                return status;
            }

            const auto element_count = entry.head().mapping_count() / group_size;
            if constexpr (fixed_count != std::dynamic_extent) {
                if (element_count != fixed_count) {
                    return fail(error_code::fixed_container_size_mismatch, std::nullopt, 8);
                }
            } else if constexpr (container_capacity<T> != std::dynamic_extent) {
                if (element_count > container_capacity<T>) {
                    return fail(error_code::resource_limit_exceeded, std::nullopt, 8);
                }
            }

            // Non-exact inputs keep the cursor's existing error precedence and offsets.
            if constexpr (fixed_group.stride != std::dynamic_extent) {
                if (entry.matches_fixed_slots<fixed_group.stride>(fixed_group.rows)) {
                    return decode_fixed_slots(entry, out, element_count, context);
                }
            }

            defined_entry::cursor input;
            auto decode_slot = [&]<std::size_t Slot>(std::size_t index, auto &value) -> error {
                auto field = entry.next(input);
                if (!field) { return field.error(); }
                const auto &slot = *field;
                constexpr auto slot_wire_type = Tree[Node, Slot].wire_type();
                if (slot.wire_type != slot_wire_type) {
                    return fail(error_code::type_mismatch, static_cast<id_type>(index), slot.offset);
                }
                if (auto status = slot_codec<Slot>::decode(
                    slot.bytes, value, context, doc); status) {
                    return detail::locate_error(status, static_cast<id_type>(index), slot.offset);
                }
                return {};
            };
            if (auto status = decode_elements(out, element_count, context, decode_slot); status) { return status; }
            return entry.finish(input);
        }

    private:
        static error decode_fixed_slots(const defined_entry &entry, value_type &out, std::size_t element_count,
                                        const serialization_context &context) {
            using element_type = typename container_traits<value_type>::element_type;
            const auto payload = entry.content();
            if constexpr (group_size == 1 && slot_codec<0>::bulk_decodable
                          && bulk_sequence_destination<value_type, element_type>) {
                // Resize constructs live elements before copying their object representations.
                if constexpr (fixed_count == std::dynamic_extent) {
                    out.clear();
                    out.resize(element_count);
                }
                if (!payload.empty()) {
                    std::memcpy(std::ranges::data(detail::container_range(out)), payload.data(), payload.size());
                }
                return {};
            } else {
                auto decode_slot = [&]<std::size_t Slot>(std::size_t index, auto &value) -> error {
                    constexpr auto width = fixed_group.rows[Slot].leaf_extent;
                    const auto offset = (index / group_size) * fixed_group.stride + fixed_group.offsets[Slot];
                    auto status = slot_codec<Slot>::decode(payload.subspan(offset).template first<width>(), value, context);
                    if constexpr (std::same_as<decltype(status), error>) {
                        if (status) {
                            return detail::locate_error(status, static_cast<id_type>(index), entry.content_offset() + offset);
                        }
                    }
                    return {};
                };
                return decode_elements(out, element_count, context, decode_slot);
            }
        }

        template<typename DecodeSlot>
        static error decode_elements(value_type &out, std::size_t element_count, const serialization_context &context,
                                     DecodeSlot &&decode_slot) {
            using T = value_type;
            using traits = container_traits<T>;
            if constexpr (fixed_count == std::dynamic_extent) {
                detail::clear_container(out);
                detail::reserve_container(out, element_count);
            }
            constexpr auto duplicate_mode = descriptor.container().duplicates;
            const auto duplicates = resolve_duplicates(duplicate_mode, context.config.duplicates);

            if constexpr (group_size == 1) {
                if constexpr (fixed_count != std::dynamic_extent) {
                    auto iterator = std::ranges::begin(detail::container_range(out));
                    for (std::size_t index = 0; index != element_count; ++index, ++iterator) {
                        if (auto decoded = decode_slot.template operator()<0>(index, *iterator); decoded) {
                            return decoded;
                        }
                    }
                } else if constexpr (detail::insert_after_container<T>) {
                    auto cursor = out.before_begin();
                    for (std::size_t index = 0; index != element_count; ++index) {
                        typename traits::element_type element{};
                        if (auto decoded = decode_slot.template operator()<0>(index, element); decoded) {
                            return decoded;
                        }
                        cursor = out.insert_after(cursor, std::move(element));
                    }
                } else {
                    for (std::size_t index = 0; index != element_count; ++index) {
                        typename traits::element_type element{};
                        if (auto decoded = decode_slot.template operator()<0>(index, element); decoded) {
                            return decoded;
                        }
                        const auto inserted = detail::insert_container_value(out, std::move(element));
                        if (!inserted
                            && duplicates == duplicate_policy::reject) {
                            return fail(
                                error_code::duplicate_container_element,
                                static_cast<id_type>(index));
                        }
                    }
                }
            } else {
                for (std::size_t index = 0; index != element_count; ++index) {
                    typename traits::key_type key{};
                    typename traits::mapped_type mapped{};
                    if (auto decoded = decode_slot.template operator()<0>(index * 2, key); decoded) {
                        return decoded;
                    }
                    if (auto decoded = decode_slot.template operator()<1>(index * 2 + 1, mapped); decoded) {
                        return decoded;
                    }
                    typename traits::element_type element{
                        std::move(key),
                        std::move(mapped),
                    };
                    const auto inserted = detail::insert_container_value(out, std::move(element));
                    if (!inserted
                        && duplicates == duplicate_policy::reject) {
                        return fail(
                            error_code::duplicate_container_element,
                            static_cast<id_type>(index * 2));
                    }
                }
            }
            return {};
        }
    };

    template<typename T>
    struct root_codec {
        static constexpr schema_tree tree = schema{^^T}.tree;
        static constexpr szt node = tree.value_index();
        static constexpr schema_node descriptor = schema{^^T}.value_node();
        static constexpr bool uses_recorder = encoding_uses_recorder(tree, node);

        template<schema_storage Mode>
        struct encoding_plan {
            static constexpr bool only = Mode == schema_storage::schema_only;
            static constexpr szt static_extent = root_encode_extent<T, Mode>.value;
            static constexpr auto document = only ? mapping_layout_for<tree>.local_header : mapping_layout_for<tree>.embedded_header;
            SRL_NO_UNIQUE_ADDRESS value_codec<tree, node>::encode_plan payload;
            SRL_NO_UNIQUE_ADDRESS cond_value<szt, static_extent == std::dynamic_extent, static_extent> size;

            using prepare_result = decltype(payload)::prepare_result;
            using encode_result = decltype(payload)::encode_result;

            prepare_result prepare(const T &value, const serialization_context &context, encoding_session *records) {
                szt measured{};
                if constexpr (auto status = payload.prepare(value, context, records, measured, only); std::same_as<decltype(status), error>) {
                    if (status) { return status; }
                }
                if constexpr (static_extent == std::dynamic_extent) {
                    if (measured > maximum_entry_size - document.size()) { return fail(error_code::entry_too_large); }
                    measured += document.size();
                    size = measured;
                }
                return {};
            }

            encode_result encode(std::span<std::byte, static_extent> destination, const T &value, encoding_cursor &records) const {
                encoding_output out(destination, only);
                out.copy(document);
                if constexpr (descriptor.kind() == schema_kind::leaf) {
                    constexpr auto type_hash_value = descriptor.wire_type();
                    constexpr auto leaf_size = entry_head_wire_size + descriptor.encode_extent().value;
                    detail::write_trivial(out.take<entry_head_wire_size>(), entry_head{
                        .size = leaf_size,
                        .metadata = {0, entry_kind::leaf},
                        .wire_type = type_hash_value,
                    });
                }
                auto status = payload.encode(out, value, records);
                if (status) {
                    constexpr auto leaf_header = descriptor.kind() == schema_kind::leaf ? entry_head_wire_size : 0;
                    status.rebase(document.size() + leaf_header);
                }
                return status;
            }
        };

        using encode_plan = encoding_plan<schema_storage::embedded>;

        [[nodiscard]] static error decode_entry(
            const defined_entry &entry, T &out, const serialization_context &context, const wire_document &doc) {
            constexpr auto type_hash_value = descriptor.wire_type();
            if constexpr (descriptor.kind() == schema_kind::leaf) {
                if (auto status = validate_entry_type(entry, entry_kind::leaf, type_hash_value); status) {
                    return status;
                }
                if (auto status = value_codec<tree, node>::decode(entry.content(), out, context); status) {
                    return status.rebase(entry_head_wire_size);
                }
                return {};
            } else {
                return value_codec<tree, node>::decode_entry(entry, out, context, doc);
            }
        }
    };
}

#undef SRL_NO_UNIQUE_ADDRESS
