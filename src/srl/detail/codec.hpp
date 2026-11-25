#pragma once

#include <array>
#include <inplace_vector>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <meta>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../cond_value.hpp"
#include "../context.hpp"
#include "../number_to_string.hpp"
#include "../schema.hpp"
#include "../storage/serialization_workspace.hpp"
#include "codec_support.hpp"

#if defined(_MSC_VER)
#define SRL_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define SRL_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace srl::detail {
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

    consteval bool encoding_needs_cache(const schema_tree &tree, std::size_t node) {
        const auto &descriptor = tree[node];
        if (descriptor.kind() == schema_kind::container && descriptor.container().element_count == 0) { return false; }
        if (descriptor.encode_extent().is_dynamic()) {
            return true;
        }
        for (auto child: tree.child_indices(node)) {
            if (tree[child].converted() || encoding_needs_cache(tree, child)) {
                return true;
            }
        }
        return false;
    }

    consteval bool encoding_uses_workspace(const schema_tree &tree, std::size_t node) {
        const auto &descriptor = tree[node];
        if (descriptor.kind() == schema_kind::container && descriptor.container().element_count == 0) { return false; }
        for (auto child: tree.child_indices(node)) {
            if ((descriptor.kind() == schema_kind::container && encoding_needs_cache(tree, child))
                || encoding_uses_workspace(tree, child)) { return true; }
        }
        return false;
    }

    template<std::meta::info Member>
    auto make_plan_member(std::pmr::memory_resource *resource) {
        using plan = [:std::meta::type_of(Member):];
        return plan{resource};
    }

    template<typename Aggregate, const auto &Members>
    Aggregate make_plan_members(std::pmr::memory_resource *resource) {
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return Aggregate{make_plan_member<Members[Indices]>(resource)...};
        }(std::make_index_sequence<Members.size()>{});
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

        static constexpr object_layout object = descriptor.leaf().layout().value_or(object_layout{});

        struct encode_plan {
            static constexpr auto size = descriptor.encode_extent().value;

            using prepare_result = always_success;
            encode_plan() = default;
            explicit encode_plan(std::pmr::memory_resource *) noexcept {}

            prepare_result prepare(const value_type &, const serialization_context &, std::pmr::memory_resource *) { return {}; }

            auto encode(std::span<std::byte, descriptor.encode_extent().value> destination, const value_type &value) const {
                if constexpr (descriptor.leaf().strategy == leaf_strategy::forced) {
                    return detail::encode_object_bytes<object>(value, destination);
                } else {
                    return leaf_codec<value_type>::encode(value, destination);
                }
            }

            using encode_result = decltype(std::declval<const encode_plan &>().encode(
                std::declval<std::span<std::byte, descriptor.encode_extent().value>>(), std::declval<const value_type &>()));
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

        template<std::size_t Extent>
        [[nodiscard]] static combined_status<status_type<Extent == descriptor.encode_extent().value>, payload_decode_result> decode(
            std::span<const std::byte, Extent> source, value_type &out,
            const serialization_context &) {
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

            encode_plan() requires (!encoding_uses_workspace(Tree, Node)) = default;
            explicit encode_plan(std::pmr::memory_resource *resource) : payload(resource) {}

            template<typename T>
            prepare_result prepare(const T &object, const serialization_context &context, std::pmr::memory_resource *resource) {
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
                    return payload.prepare(*storage, context, resource);
                } else {
                    return payload.prepare(mem, context, resource);
                }
            }

            template<typename T>
            encode_result encode(std::span<std::byte, descriptor.encode_extent().value> destination, const T &object) const {
                if constexpr (descriptor.converted()) {
                    return payload.encode(destination, *storage);
                } else {
                    return payload.encode(destination, detail::entity_reference<descriptor.field().entity, (descriptor.field().is_base() ? reflected_entity_kind::base : reflected_entity_kind::member)>(object));
                }
            }
        };

        template<typename T>
        [[nodiscard]] static error decode(
            const field_view &field, T &out, const serialization_context &context) {
            if (field.wire_type != descriptor.wire_type()) {
                return fail(error_code::type_mismatch, detail::diagnostic_id(descriptor), field.offset);
            }
            auto &mem = detail::entity_reference<descriptor.field().entity, (descriptor.field().is_base() ? reflected_entity_kind::base : reflected_entity_kind::member)>(out);
            if constexpr (descriptor.converted()) {
                storage_type storage{};
                if constexpr (descriptor.field().conversion != schema_conversion_kind::handler) {
                    if (auto status = value_codec<Tree, Node>::decode(field.bytes, storage, context); status) {
                        return detail::locate_error(status, detail::diagnostic_id(descriptor), field.offset);
                    }
                    mem = static_cast<memory_type>(std::move(storage));
                } else {
                    constexpr auto handler_id = descriptor.field().handler_id;
                    auto &handler = context.template get_handler<memory_type, storage_type>(handler_id);
                    if (auto status = value_codec<Tree, Node>::decode(field.bytes, storage, context); status) {
                        return detail::locate_error(status, detail::diagnostic_id(descriptor), field.offset);
                    }
                    if (auto status = handler.srl2mem(std::addressof(mem), std::addressof(storage)); status) {
                        return detail::locate_error(status, detail::diagnostic_id(descriptor), field.offset);
                    }
                }
            } else {
                if (auto status = value_codec<Tree, Node>::decode(field.bytes, mem, context); status) {
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
        const auto children = tree[node].kind() == schema_kind::container && tree[node].container().element_count == 0
            ? std::span<const std::size_t>{} : tree.child_indices(node);
        for (auto child: children) {
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
            SRL_NO_UNIQUE_ADDRESS cond_value<szt, descriptor.encode_extent().value == std::dynamic_extent, descriptor.encode_extent().value>
                size{record_content_offset(descriptor.kind(), members.size())};

            encode_plan() requires (!encoding_uses_workspace(Tree, Node)) = default;
            explicit encode_plan(std::pmr::memory_resource *resource)
                : fields(make_plan_members<fields_type, members>(resource)) {}

            prepare_result prepare(const value_type &value, const serialization_context &context, std::pmr::memory_resource *resource) {
                template for (constexpr auto ordinal: std::views::iota(std::size_t{}, members.size())) {
                    constexpr auto member = members[ordinal];
                    static constexpr schema_node field_node = Tree[Node, ordinal];
                    auto &field = fields.[:member:];
                    if constexpr (auto status = field.prepare(value, context, resource); std::same_as<decltype(status), error>) {
                        if (status) { return detail::locate_error(status, detail::diagnostic_id(field_node)); }
                    }
                    if constexpr (descriptor.encode_extent().value == std::dynamic_extent) {
                        if (auto status = detail::append_encoded_size(size, field.payload.size, 0, field_node.field_id().value_or(0)); status) {
                            return status;
                        }
                    }
                }
                return {};
            }

            encode_result encode(std::span<std::byte, descriptor.encode_extent().value> destination, const value_type &value) const {
                constexpr auto wire_kind = descriptor.wire_kind();
                detail::write_trivial(destination, entry_head{
                    .size = size,
                    .metadata = {(descriptor.kind() == schema_kind::stream) ? 0 : members.size(), wire_kind},
                    .wire_type = descriptor.wire_type(),
                });
                szt address{};
                std::size_t index{};
                template for (constexpr auto ordinal: std::views::iota(std::size_t{}, members.size())) {
                    constexpr auto member = members[ordinal];
                    static constexpr schema_node field_node = Tree[Node, ordinal];
                    const auto &field = fields.[:member:];
                    if constexpr (descriptor.kind() != schema_kind::stream) {
                        detail::write_trivial(destination.subspan(entry_head_wire_size + index++ * entry_mapping_wire_size),
                            entry_mapping{.id = field_node.record_field().id, .wire_type = field_node.wire_type(), .offset = address});
                    }
                    if constexpr (auto status = field.encode(
                        detail::payload_span<field_node.encode_extent().value>(destination, record_content_offset(descriptor.kind(), members.size()) + address, field.payload.size), value);
                        std::same_as<decltype(status), error>) {
                        if (status) { return detail::locate_error(status, detail::diagnostic_id(field_node), record_content_offset(descriptor.kind(), members.size()) + address); }
                    }
                    address += field.payload.size;
                }
                return {};
            }
        };

        [[nodiscard]] static error decode(
            std::span<const std::byte> source, value_type &out,
            const serialization_context &context) {
            auto entry = parse_entry(source);
            return entry ? value_codec::decode_entry(*entry, out, context) : entry.error();
        }

        [[nodiscard]] static error decode_entry(
            const entry_view &entry, value_type &out,
            const serialization_context &context) {
            if (auto status = validate_entry_type(entry, descriptor.wire_kind(), descriptor.wire_type()); status) {
                return status;
            }
            szt cursor{};
            std::size_t mapping_cursor{};
            template for (constexpr auto child: Tree.child_indices(Node)) {
                static constexpr schema_node field_node = Tree[child];
                constexpr auto field_extent = field_node.decode_extent().value;
                constexpr auto field_wire_type = field_node.wire_type();
                auto field = [&]() -> std::expected<field_view, error> {
                    if constexpr (descriptor.kind() != schema_kind::stream) {
                        constexpr auto id = field_node.record_field().id;
                        return entry.find(id, mapping_cursor);
                    } else {
                        constexpr bool has_header = field_node.kind() != schema_kind::leaf;
                        return next_stream_field(entry, cursor, field_extent, field_wire_type, has_header);
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
                if (auto status = field_codec<Tree, child>::decode(*field, out, context); status) {
                    return status;
                }
            }
            if constexpr (descriptor.kind() == schema_kind::stream) {
                if (cursor != entry.content().size()) {
                    return fail(error_code::trailing_bytes, std::nullopt, entry.content_offset() + cursor);
                }
            }
            return {};
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
    struct container_slot_plans {
        using value_type = node_value_t<Tree, Node>;

        std::pmr::vector<typename value_codec<Tree, Node>::encode_plan> plans;

        using prepare_result = value_codec<Tree, Node>::encode_plan::prepare_result;
        using encode_result = value_codec<Tree, Node>::encode_plan::encode_result;

        explicit container_slot_plans(std::pmr::memory_resource *resource) : plans(resource) {}

        void reserve(std::size_t count) { plans.reserve(count); }

        prepare_result prepare(const value_type &value, const serialization_context &context, std::pmr::memory_resource *resource) {
            auto &plan = plans.emplace_back(resource);
            return plan.prepare(value, context, resource);
        }

        szt size(std::size_t index) const noexcept { return plans[index].size; }

        encode_result encode(std::size_t index, std::span<std::byte, Tree[Node].encode_extent().value> destination, const value_type &value) const {
            return plans[index].encode(destination, value);
        }
    };

    template<const schema_tree &Tree, std::size_t Node>
        requires (!encoding_needs_cache(Tree, Node))
    struct container_slot_plans<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;

        using prepare_result = always_success;
        using encode_result = value_codec<Tree, Node>::encode_plan::encode_result;

        container_slot_plans() = default;
        explicit container_slot_plans(std::pmr::memory_resource *) noexcept {}

        void reserve(std::size_t) noexcept {}

        prepare_result prepare(const value_type &, const serialization_context &, std::pmr::memory_resource *) { return {}; }

        constexpr szt size(std::size_t) const noexcept {
            constexpr szt extent = Tree[Node].encode_extent().value;
            return extent;
        }

        encode_result encode(std::size_t, std::span<std::byte, Tree[Node].encode_extent().value> destination, const value_type &value) const {
            return typename value_codec<Tree, Node>::encode_plan{}.encode(destination, value);
        }
    };

    template<typename T>
    inline constexpr std::size_t container_capacity = std::dynamic_extent;

    template<typename T, std::size_t Capacity>
    inline constexpr std::size_t container_capacity<std::inplace_vector<T, Capacity>> = Capacity;

    template<const schema_tree &Tree, std::size_t Node>
        requires (Tree[Node].kind() == schema_kind::container)
    struct value_codec<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr schema_node descriptor = Tree[Node];

        struct encode_plan {
            struct slots_type;
            consteval {
                define_plan_members(^^slots_type, ^^container_slot_plans, std::meta::reflect_object(Tree), Tree, Node);
            }
            static constexpr std::span<const std::meta::info> members = std::define_static_array(
                std::meta::nonstatic_data_members_of(^^slots_type, std::meta::access_context::current()));

            SRL_NO_UNIQUE_ADDRESS slots_type slots;
            using prepare_result = status_type<descriptor.container().element_count == 0
                || (descriptor.container().element_count != std::dynamic_extent && descriptor.encode_extent().value != std::dynamic_extent
                    && plans_always_succeed<members, true>())>;
            using encode_result = status_type<descriptor.container().element_count == 0
                || plans_always_succeed<members, false>()>;
            SRL_NO_UNIQUE_ADDRESS cond_value<
                szt,
                descriptor.encode_extent().value == std::dynamic_extent,
                descriptor.encode_extent().value>
            size{entry_head_wire_size};

            SRL_NO_UNIQUE_ADDRESS cond_value<
                std::uint32_t,
                descriptor.container().element_count == std::dynamic_extent,
                descriptor.container().element_count == std::dynamic_extent ? 0 : static_cast<std::uint32_t>(descriptor.container().element_count * descriptor.container().slot_count())>
            slot_count;

            encode_plan() requires (!encoding_uses_workspace(Tree, Node)) = default;
            explicit encode_plan(std::pmr::memory_resource *resource)
                : slots(make_plan_members<slots_type, members>(resource)) {}

            void reserve_slots(std::size_t count) {
                template for (constexpr auto member: members) {
                    slots.[:member:].reserve(count);
                }
            }

            static constexpr bool stateless_slots = [] consteval {
                for (auto child: Tree.child_indices(Node)) {
                    if (encoding_needs_cache(Tree, child)) { return false; }
                }
                return true;
            }();

            prepare_result prepare(const value_type &value, const serialization_context &context, std::pmr::memory_resource *resource) {
                constexpr auto group_size = descriptor.container().slot_count();
                if constexpr (stateless_slots && descriptor.container().element_count != std::dynamic_extent) {
                    return {};
                } else if constexpr (stateless_slots && std::ranges::sized_range<decltype(detail::container_range(value))>) {
                    static constexpr auto extents = [] consteval {
                        std::array<szt, descriptor.container().slot_count()> result;
                        for (std::size_t i = 0; i != result.size(); ++i) {
                            result[i] = Tree[Node, i].encode_extent().value;
                        }
                        return result;
                    }();
                    const auto count = std::ranges::size(detail::container_range(value));
                    auto measured = measure_fixed_slots(count, extents);
                    if (!measured) { return measured.error(); }
                    size = *measured;
                    slot_count = static_cast<std::uint32_t>(count * group_size);
                    return {};
                }
                prepare_result status{};
                std::uint32_t slot_index{};
                if constexpr (std::ranges::sized_range<decltype(detail::container_range(value))>) {
                    const auto count = std::ranges::size(detail::container_range(value));
                    if constexpr (descriptor.container().element_count == std::dynamic_extent) {
                        if (count > entry_metadata::max_mapping_count / group_size) {
                            return fail(error_code::too_many_mappings, entry_metadata::max_mapping_count);
                        }
                    }
                    reserve_slots(count);
                }
                if constexpr (descriptor.container().element_count != 0) {
                    detail::for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) -> bool {
                        if constexpr (std::same_as<prepare_result, error>) {
                            if (status) { return false; }
                        }
                        if constexpr (descriptor.container().element_count == std::dynamic_extent) {
                            if (slot_index == entry_metadata::max_mapping_count) {
                                status = fail(error_code::too_many_mappings, slot_index);
                                return false;
                            }
                        }
                        auto &plans = slots.[:members[Slot]:];
                        if constexpr (auto prepared = plans.prepare(slot, context, resource); std::same_as<decltype(prepared), error>) {
                            if (prepared) {
                                status = detail::locate_error(prepared, slot_index);
                                return false;
                            }
                        }
                        if constexpr (descriptor.encode_extent().value == std::dynamic_extent) {
                            const auto plan_size = plans.size(slot_index / group_size);
                            if (auto failure = append_encoded_size(size, plan_size, entry_mapping_wire_size, slot_index); failure) {
                                status = failure;
                                return false;
                            }
                        }
                        ++slot_index;
                        return true;
                    });
                }
                slot_count = slot_index;
                return status;
            }

            encode_result encode(std::span<std::byte, descriptor.encode_extent().value> destination, const value_type &value) const {
                constexpr auto group_size = descriptor.container().slot_count();
                constexpr auto wire_kind = descriptor.wire_kind();
                detail::write_trivial(destination, entry_head{
                    .size = size,
                    .metadata = {slot_count, wire_kind},
                    .wire_type = descriptor.wire_type(),
                });
                const auto content_offset = entry_head_wire_size
                    + static_cast<std::size_t>(slot_count) * entry_mapping_wire_size;
                std::size_t index{};
                szt address{};
                encode_result status{};
                if constexpr (descriptor.container().element_count != 0) {
                    detail::for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) -> bool {
                        if constexpr (std::same_as<encode_result, error>) {
                            if (status) { return false; }
                        }
                        const auto &plans = slots.[:members[Slot]:];
                        constexpr type_hash_type slot_wire_type = Tree[Node, Slot].wire_type();
                        constexpr szt slot_extent = Tree[Node, Slot].encode_extent().value;
                        const auto plan_index = index / group_size;
                        const auto plan_size = plans.size(plan_index);
                        detail::write_trivial(destination.subspan(entry_head_wire_size + index * entry_mapping_wire_size),
                            entry_mapping{.id = static_cast<id_type>(index), .wire_type = slot_wire_type, .offset = address});
                        if constexpr (auto encoded = plans.encode(plan_index,
                            detail::payload_span<slot_extent>(destination, content_offset + address, plan_size), slot);
                            std::same_as<decltype(encoded), error>) {
                            if (encoded) {
                                status = locate_error(encoded, static_cast<id_type>(index), content_offset + address);
                                return false;
                            }
                        }
                        address += plan_size;
                        ++index;
                        return true;
                    });
                }
                return status;
            }
        };

        [[nodiscard]] static error decode(
            std::span<const std::byte> source, value_type &out,
            const serialization_context &context) {
            auto entry = parse_entry(source);
            return entry ? value_codec::decode_entry(*entry, out, context) : entry.error();
        }

        [[nodiscard]] static error decode_entry(
            const entry_view &entry, value_type &out,
            const serialization_context &context) {
            using T = value_type;
            using traits = container_traits<T>;
            constexpr auto group_size = descriptor.container().slot_count();
            constexpr auto fixed_count = descriptor.container().element_count;
            constexpr auto duplicate_mode = descriptor.container().duplicates;
            if (auto status = validate_entry_type(entry, descriptor.wire_kind(), descriptor.wire_type()); status) {
                return status;
            }

            const auto element_count = entry.head().mapping_count() / group_size;
            if constexpr (fixed_count != std::dynamic_extent) {
                if (element_count != fixed_count) {
                    return fail(error_code::fixed_container_size_mismatch, std::nullopt, 8);
                }
            } else {
                if constexpr (container_capacity<T> != std::dynamic_extent) {
                    if (element_count > container_capacity<T>) {
                        return fail(error_code::resource_limit_exceeded, std::nullopt, 8);
                    }
                }
                detail::clear_container(out);
                detail::reserve_container(out, element_count);
            }

            const auto duplicates = resolve_duplicates(duplicate_mode, context.config.duplicates);
            auto decode_slot = [&]<std::size_t Slot>(std::size_t index, auto &value) -> error {
                const auto slot = entry.at(index);
                constexpr auto slot_wire_type = Tree[Node, Slot].wire_type();
                if (slot.wire_type != slot_wire_type) {
                    return fail(error_code::type_mismatch, static_cast<id_type>(index), slot.offset);
                }
                if (auto status = value_codec<Tree, Tree.child_indices(Node)[Slot]>::decode(
                    slot.bytes, value, context); status) {
                    return detail::locate_error(status, static_cast<id_type>(index), slot.offset);
                }
                return {};
            };

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
        static constexpr schema_tree tree = schema_for<T>.tree;
        static constexpr szt node = tree.value_index();
        static constexpr schema_node descriptor = schema_for<T>.value_node();
        static constexpr bool uses_workspace = encoding_uses_workspace(tree, node);

        struct encode_plan {
            static constexpr szt static_extent = root_encode_extent<T>.value;
            SRL_NO_UNIQUE_ADDRESS value_codec<tree, node>::encode_plan payload;
            SRL_NO_UNIQUE_ADDRESS cond_value<szt, static_extent == std::dynamic_extent, static_extent> size;

            using prepare_result = decltype(payload)::prepare_result;
            using encode_result = decltype(payload)::encode_result;

            encode_plan() requires (!encoding_uses_workspace(tree, node)) = default;
            explicit encode_plan(std::pmr::memory_resource *resource) : payload(resource) {}

            prepare_result prepare(const T &value, const serialization_context &context, std::pmr::memory_resource *resource) {
                if constexpr (auto status = payload.prepare(value, context, resource); std::same_as<decltype(status), error>) {
                    if (status) { return status; }
                }
                if constexpr (static_extent == std::dynamic_extent) {
                    size = payload.size;
                }
                return {};
            }

            encode_result encode(std::span<std::byte, static_extent> destination, const T &value) const {
                if constexpr (descriptor.kind() == schema_kind::leaf) {
                    constexpr auto type_hash_value = descriptor.wire_type();
                    detail::write_trivial(destination, entry_head{
                        .size = size,
                        .metadata = {0, entry_kind::leaf},
                        .wire_type = type_hash_value,
                    });
                    return payload.encode(destination.template subspan<entry_head_wire_size>(), value);
                } else {
                    return payload.encode(destination, value);
                }
            }
        };

        [[nodiscard]] static error decode_entry(
            const entry_view &entry, T &out, const serialization_context &context) {
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
                return value_codec<tree, node>::decode_entry(entry, out, context);
            }
        }
    };
}

#undef SRL_NO_UNIQUE_ADDRESS
