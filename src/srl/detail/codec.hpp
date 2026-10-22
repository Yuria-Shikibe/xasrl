#pragma once

#include <charconv>
#include <iterator>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <memory>
#include <meta>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../cond_value.hpp"
#include "../context.hpp"
#include "../schema.hpp"
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

    template<const schema_tree &Tree, std::size_t Node>
    using node_value_t = [:Tree.node(Node).type:];

    template<const schema_tree &Tree, std::size_t Node>
    struct value_codec;

    template<const schema_tree &Tree, std::size_t Node>
        requires (Tree.node(Node).kind == schema_kind::leaf)
    struct value_codec<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr schema_node descriptor = Tree.node(Node);

        static constexpr object_layout object = descriptor.object;

        struct encode_plan {
            static constexpr auto size = descriptor.encode_extent.value;

            using prepare_result = always_success;
            prepare_result prepare(const value_type &, const serialization_context &) { return {}; }

            auto encode(std::span<std::byte, descriptor.encode_extent.value> destination, const value_type &value) const {
                if constexpr (descriptor.representation == leaf_representation::forced) {
                    return detail::encode_object_bytes<object>(value, destination);
                } else {
                    return leaf_codec<value_type>::encode(value, destination);
                }
            }

            using encode_result = decltype(std::declval<const encode_plan &>().encode(
                std::declval<std::span<std::byte, descriptor.encode_extent.value>>(), std::declval<const value_type &>()));
        };

        static auto decode_payload(std::span<const std::byte, descriptor.encode_extent.value> source, value_type &out) {
            if constexpr (descriptor.representation == leaf_representation::forced || descriptor.builtin_object_codec) {
                return detail::decode_object_bytes<object>(source, out);
            } else if constexpr (descriptor.builtin_scalar_codec) {
                return scalar_codec<value_type>::template decode_with_validation<descriptor.validation>(source, out);
            } else {
                return leaf_codec<value_type>::decode(source, out);
            }
        }

        using payload_decode_result = decltype(value_codec::decode_payload(
            std::declval<std::span<const std::byte, descriptor.encode_extent.value>>(), std::declval<value_type &>()));

        template<std::size_t Extent>
        [[nodiscard]] static combined_status<status_type<Extent == descriptor.encode_extent.value>, payload_decode_result> decode(
            std::span<const std::byte, Extent> source, value_type &out,
            const serialization_context &) {
            // Validate untrusted lengths before forming the fixed-size codec span.
            constexpr auto extent = descriptor.decode_extent.value;
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
        static constexpr schema_node descriptor = Tree.node(Node);

        using memory_type = [:descriptor.memory_type:];
        using storage_type = node_value_t<Tree, Node>;

        struct encode_plan {
            SRL_NO_UNIQUE_ADDRESS [:descriptor.converted() ? ^^std::optional<storage_type> : ^^no_conversion:] storage;
            SRL_NO_UNIQUE_ADDRESS value_codec<Tree, Node>::encode_plan payload;

            using prepare_result = combined_status<
                status_type<descriptor.conversion.handler_id == nullptr>, typename decltype(payload)::prepare_result>;
            using encode_result = decltype(payload)::encode_result;

            template<typename T>
            prepare_result prepare(const T &object, const serialization_context &context) {
                const auto &mem = detail::entity_reference<descriptor.entity, (descriptor.role == schema_role::base ? reflected_entity_kind::base : reflected_entity_kind::member)>(object);
                if constexpr (descriptor.converted()) {
                    if constexpr (descriptor.conversion.handler_id == nullptr) {
                        storage.emplace(static_cast<storage_type>(mem));
                    } else {
                        auto &handler = context.template get_handler<memory_type, storage_type>(descriptor.conversion.handler_id);
                        storage.emplace();
                        if (auto status = handler.mem2srl(std::addressof(mem), std::addressof(*storage)); status) {
                            return status;
                        }
                    }
                    return payload.prepare(*storage, context);
                } else {
                    return payload.prepare(mem, context);
                }
            }

            template<typename T>
            encode_result encode(std::span<std::byte, descriptor.encode_extent.value> destination, const T &object) const {
                if constexpr (descriptor.converted()) {
                    return payload.encode(destination, *storage);
                } else {
                    return payload.encode(destination, detail::entity_reference<descriptor.entity, (descriptor.role == schema_role::base ? reflected_entity_kind::base : reflected_entity_kind::member)>(object));
                }
            }
        };

        template<typename T>
        [[nodiscard]] static error decode(
            const field_view &field, T &out, const serialization_context &context) {
            if (field.wire_type != descriptor.wire_type) {
                return fail(error_code::type_mismatch, detail::diagnostic_id(descriptor), field.offset);
            }
            auto &mem = detail::entity_reference<descriptor.entity, (descriptor.role == schema_role::base ? reflected_entity_kind::base : reflected_entity_kind::member)>(out);
            if constexpr (descriptor.converted()) {
                storage_type storage{};
                if constexpr (descriptor.conversion.handler_id == nullptr) {
                    if (auto status = value_codec<Tree, Node>::decode(field.bytes, storage, context); status) {
                        return detail::locate_error(status, detail::diagnostic_id(descriptor), field.offset);
                    }
                    mem = static_cast<memory_type>(std::move(storage));
                } else {
                    auto &handler = context.template get_handler<memory_type, storage_type>(descriptor.conversion.handler_id);
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
        for (auto child: tree.children(node)) {
            const auto type = std::meta::substitute(plan_template,
                {tree_reference, std::meta::reflect_constant(child)});
            char digits[std::numeric_limits<std::size_t>::digits10 + 1];
            const auto result = std::to_chars(std::begin(digits), std::end(digits), members.size());
            std::string name = "field_";
            name.append(digits, result.ptr);
            members.push_back(std::meta::data_member_spec(type, {.name = name, .no_unique_address = true}));
        }
        return std::meta::define_aggregate(target, members);
    }

    template<const schema_tree &Tree, std::size_t Node>
        requires (Tree.node(Node).kind == schema_kind::record || Tree.node(Node).kind == schema_kind::stream)
    struct value_codec<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr schema_node descriptor = Tree.node(Node);

        struct encode_plan {
            struct fields_type;
            consteval {
                define_plan_members(^^fields_type, ^^field_encode_plan, std::meta::reflect_object(Tree), Tree, Node);
            }
            static constexpr std::span<const std::meta::info> members = std::define_static_array(
                std::meta::nonstatic_data_members_of(^^fields_type, std::meta::access_context::current()));

            SRL_NO_UNIQUE_ADDRESS fields_type fields;

            using prepare_result = status_type<descriptor.encode_extent.value != std::dynamic_extent
                && plans_always_succeed<members, true>()>;
            using encode_result = status_type<plans_always_succeed<members, false>()>;
            SRL_NO_UNIQUE_ADDRESS cond_value<szt, descriptor.encode_extent.value == std::dynamic_extent, descriptor.encode_extent.value>
                size{record_content_offset(descriptor)};

            prepare_result prepare(const value_type &value, const serialization_context &context) {
                template for (constexpr auto ordinal: std::views::iota(std::size_t{}, members.size())) {
                    constexpr auto member = members[ordinal];
                    static constexpr schema_node field_node = Tree.node(Tree.child(Node, ordinal));
                    auto &field = fields.[:member:];
                    if constexpr (auto status = field.prepare(value, context); std::same_as<decltype(status), error>) {
                        if (status) { return detail::locate_error(status, detail::diagnostic_id(field_node)); }
                    }
                    if constexpr (descriptor.encode_extent.value == std::dynamic_extent) {
                        if (auto status = append_encoded_size(size, field.payload.size, 0, field_node.id); status) {
                            return status;
                        }
                    }
                }
                return {};
            }

            encode_result encode(std::span<std::byte, descriptor.encode_extent.value> destination, const value_type &value) const {
                constexpr auto wire_kind = descriptor.wire_kind;
                detail::write_trivial(destination, entry_head{
                    .size = size,
                    .metadata = {(descriptor.kind == schema_kind::stream) ? 0 : descriptor.children_count, wire_kind},
                    .wire_type = descriptor.wire_type,
                });
                szt address{};
                std::size_t index{};
                template for (constexpr auto ordinal: std::views::iota(std::size_t{}, members.size())) {
                    constexpr auto member = members[ordinal];
                    static constexpr schema_node field_node = Tree.node(Tree.child(Node, ordinal));
                    const auto &field = fields.[:member:];
                    if constexpr (descriptor.kind != schema_kind::stream) {
                        detail::write_trivial(destination.subspan(entry_head_wire_size + index++ * entry_mapping_wire_size),
                            entry_mapping{.id = field_node.id, .wire_type = field_node.wire_type, .offset = address});
                    }
                    if constexpr (auto status = field.encode(
                        detail::payload_span<field_node.encode_extent.value>(destination, record_content_offset(descriptor) + address, field.payload.size), value);
                        std::same_as<decltype(status), error>) {
                        if (status) { return detail::locate_error(status, detail::diagnostic_id(field_node), record_content_offset(descriptor) + address); }
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
            if (auto status = validate_entry_type(entry, descriptor.wire_kind, descriptor.wire_type); status) {
                return status;
            }
            szt cursor{};
            template for (constexpr auto child: Tree.children(Node)) {
                static constexpr schema_node field_node = Tree.node(child);
                constexpr auto field_id = field_node.id;
                constexpr auto field_extent = field_node.decode_extent.value;
                constexpr auto field_wire_type = field_node.wire_type;
                auto field = [&]() -> std::expected<field_view, error> {
                    if constexpr (descriptor.kind != schema_kind::stream) {
                        return entry.find(field_id);
                    } else {
                        return next_stream_field(entry, cursor, field_extent, field_wire_type);
                    }
                }();
                if (!field) {
                    if constexpr (descriptor.kind != schema_kind::stream && field_node.fallback_type != std::meta::info{}) {
                        if (field.error().code == error_code::missing_field) {
                            using fallback = [:field_node.fallback_type:];
                            constexpr auto kind = field_node.role == schema_role::base
                                ? reflected_entity_kind::base : reflected_entity_kind::member;
                            fallback::apply(entity_reference<field_node.entity, kind>(out), context);
                            continue;
                        }
                    }
                    return field.error();
                }
                if (auto status = field_codec<Tree, child>::decode(*field, out, context); status) {
                    return status;
                }
            }
            if constexpr (descriptor.kind == schema_kind::stream) {
                if (cursor != entry.content().size()) {
                    return fail(error_code::trailing_bytes, std::nullopt, entry.content_offset() + cursor);
                }
            }
            return {};
        }
    };

    template<std::size_t Slot, typename Expected, typename Value, typename Visitor>
    void visit_container_slot(Value &&value, Visitor &&visitor) {
        if constexpr (std::meta::is_same_type(std::meta::remove_cvref(^^Value), ^^Expected)) {
            visitor.template operator()<Slot>(value);
        } else {
            Expected converted(std::forward<Value>(value));
            visitor.template operator()<Slot>(converted);
        }
    }

    template<container_serializable T, typename Visitor>
    void for_each_container_slot(const T &value, Visitor &&visitor) {
        using traits = container_traits<T>;
        for (const auto &element: detail::container_range(value)) {
            if constexpr (traits::mapping_group_size == 1) {
                detail::visit_container_slot<0, typename traits::element_type>(element, visitor);
            } else {
                if constexpr (requires { traits::key(element); traits::mapped(element); }) {
                    detail::visit_container_slot<0, typename traits::key_type>(traits::key(element), visitor);
                    detail::visit_container_slot<1, typename traits::mapped_type>(traits::mapped(element), visitor);
                } else {
                    detail::visit_container_slot<0, typename traits::key_type>(element.first, visitor);
                    detail::visit_container_slot<1, typename traits::mapped_type>(element.second, visitor);
                }
            }
        }
    }

    consteval bool encoding_needs_cache(const schema_tree &tree, std::size_t node) {
        const auto &descriptor = tree.node(node);
        if (descriptor.kind == schema_kind::container || descriptor.encode_extent.is_dynamic()) {
            return true;
        }
        for (auto child: tree.children(node)) {
            if (tree.node(child).converted() || encoding_needs_cache(tree, child)) {
                return true;
            }
        }
        return false;
    }

    template<const schema_tree &Tree, std::size_t Node>
    struct container_slot_plans {
        using value_type = node_value_t<Tree, Node>;

        std::vector<typename value_codec<Tree, Node>::encode_plan> plans;

        using prepare_result = value_codec<Tree, Node>::encode_plan::prepare_result;
        using encode_result = value_codec<Tree, Node>::encode_plan::encode_result;

        prepare_result prepare(const value_type &value, const serialization_context &context) {
            auto &plan = plans.emplace_back();
            return plan.prepare(value, context);
        }

        szt size(std::size_t index) const noexcept { return plans[index].size; }

        encode_result encode(std::size_t index, std::span<std::byte, Tree.node(Node).encode_extent.value> destination, const value_type &value) const {
            return plans[index].encode(destination, value);
        }
    };

    template<const schema_tree &Tree, std::size_t Node>
        requires (!encoding_needs_cache(Tree, Node))
    struct container_slot_plans<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;

        using prepare_result = always_success;
        using encode_result = value_codec<Tree, Node>::encode_plan::encode_result;

        prepare_result prepare(const value_type &, const serialization_context &) { return {}; }

        constexpr szt size(std::size_t) const noexcept {
            constexpr szt extent = Tree.node(Node).encode_extent.value;
            return extent;
        }

        encode_result encode(std::size_t, std::span<std::byte, Tree.node(Node).encode_extent.value> destination, const value_type &value) const {
            return typename value_codec<Tree, Node>::encode_plan{}.encode(destination, value);
        }
    };

    template<const schema_tree &Tree, std::size_t Node>
        requires (Tree.node(Node).kind == schema_kind::container)
    struct value_codec<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr schema_node descriptor = Tree.node(Node);

        struct encode_plan {
            struct slots_type;
            consteval {
                define_plan_members(^^slots_type, ^^container_slot_plans, std::meta::reflect_object(Tree), Tree, Node);
            }
            static constexpr std::span<const std::meta::info> members = std::define_static_array(
                std::meta::nonstatic_data_members_of(^^slots_type, std::meta::access_context::current()));

            SRL_NO_UNIQUE_ADDRESS slots_type slots;
            using prepare_result = status_type<descriptor.element_count == 0
                || (descriptor.element_count != std::dynamic_extent && descriptor.encode_extent.value != std::dynamic_extent
                    && plans_always_succeed<members, true>())>;
            using encode_result = status_type<descriptor.element_count == 0
                || plans_always_succeed<members, false>()>;
            SRL_NO_UNIQUE_ADDRESS cond_value<
                szt,
                descriptor.encode_extent.value == std::dynamic_extent,
                descriptor.encode_extent.value>
            size{entry_head_wire_size};

            SRL_NO_UNIQUE_ADDRESS cond_value<
                std::uint32_t,
                descriptor.element_count == std::dynamic_extent,
                descriptor.element_count == std::dynamic_extent ? 0 : static_cast<std::uint32_t>(descriptor.element_count * descriptor.mapping_group_size)>
            slot_count;

            prepare_result prepare(const value_type &value, const serialization_context &context) {
                prepare_result status{};
                std::uint32_t slot_index{};
                if constexpr (descriptor.element_count != 0) {
                    detail::for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) {
                        if constexpr (std::same_as<prepare_result, error>) {
                            if (status) { return; }
                        }
                        if constexpr (descriptor.element_count == std::dynamic_extent) {
                            if (slot_index == entry_metadata::max_mapping_count) {
                                status = fail(error_code::too_many_mappings, slot_index);
                                return;
                            }
                        }
                        auto &plans = slots.[:members[Slot]:];
                        if constexpr (auto prepared = plans.prepare(slot, context); std::same_as<decltype(prepared), error>) {
                            if (prepared) {
                                status = detail::locate_error(prepared, slot_index);
                                return;
                            }
                        }
                        if constexpr (descriptor.encode_extent.value == std::dynamic_extent) {
                            const auto plan_size = plans.size(slot_index / descriptor.mapping_group_size);
                            if (auto failure = append_encoded_size(size, plan_size, entry_mapping_wire_size, slot_index); failure) {
                                status = failure;
                                return;
                            }
                        }
                        ++slot_index;
                    });
                }
                slot_count = slot_index;
                return status;
            }

            encode_result encode(std::span<std::byte, descriptor.encode_extent.value> destination, const value_type &value) const {
                constexpr auto group_size = descriptor.mapping_group_size;
                constexpr auto wire_kind = descriptor.wire_kind;
                detail::write_trivial(destination, entry_head{
                    .size = size,
                    .metadata = {slot_count, wire_kind},
                    .wire_type = descriptor.wire_type,
                });
                const auto content_offset = entry_head_wire_size
                    + static_cast<std::size_t>(slot_count) * entry_mapping_wire_size;
                std::size_t index{};
                szt address{};
                encode_result status{};
                if constexpr (descriptor.element_count != 0) {
                    detail::for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) {
                        if constexpr (std::same_as<encode_result, error>) {
                            if (status) { return; }
                        }
                        const auto &plans = slots.[:members[Slot]:];
                        constexpr type_hash_type slot_wire_type = Tree.node(Tree.child(Node, Slot)).wire_type;
                        constexpr szt slot_extent = Tree.node(Tree.child(Node, Slot)).encode_extent.value;
                        const auto plan_index = index / group_size;
                        const auto plan_size = plans.size(plan_index);
                        detail::write_trivial(destination.subspan(entry_head_wire_size + index * entry_mapping_wire_size),
                            entry_mapping{.id = static_cast<id_type>(index), .wire_type = slot_wire_type, .offset = address});
                        if constexpr (auto encoded = plans.encode(plan_index,
                            detail::payload_span<slot_extent>(destination, content_offset + address, plan_size), slot);
                            std::same_as<decltype(encoded), error>) {
                            if (encoded) {
                                status = locate_error(encoded, static_cast<id_type>(index), content_offset + address);
                                return;
                            }
                        }
                        address += plan_size;
                        ++index;
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
            constexpr auto group_size = descriptor.mapping_group_size;
            constexpr auto fixed_count = descriptor.element_count;
            constexpr auto duplicate_mode = descriptor.duplicates;
            if (auto status = validate_entry_type(entry, descriptor.wire_kind, descriptor.wire_type); status) {
                return status;
            }

            const auto element_count = entry.head().mapping_count() / group_size;
            if constexpr (fixed_count != std::dynamic_extent) {
                if (element_count != fixed_count) {
                    return fail(error_code::fixed_container_size_mismatch, std::nullopt, 8);
                }
            } else {
                detail::clear_container(out);
                detail::reserve_container(out, element_count);
            }

            const auto duplicates = resolve_duplicates(duplicate_mode, context.config.duplicates);
            auto decode_slot = [&]<std::size_t Slot>(std::size_t index, auto &value) -> error {
                const auto slot = entry.at(index);
                constexpr auto slot_wire_type = Tree.node(Tree.child(Node, Slot)).wire_type;
                if (slot.wire_type != slot_wire_type) {
                    return fail(error_code::type_mismatch, static_cast<id_type>(index), slot.offset);
                }
                if (auto status = value_codec<Tree, Tree.child(Node, Slot)>::decode(
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
        static constexpr schema_tree tree = schema(^^T).tree;
        static constexpr szt node = tree.child(tree.root, 0); //TODO rename it as node_idx ?

        struct encode_plan {
            static constexpr szt static_extent = schema(^^T).tree.node(schema(^^T).root).encode_extent.value;
            SRL_NO_UNIQUE_ADDRESS value_codec<tree, node>::encode_plan payload;
            SRL_NO_UNIQUE_ADDRESS cond_value<szt, static_extent == std::dynamic_extent, static_extent> size;

            using prepare_result = decltype(payload)::prepare_result;
            using encode_result = decltype(payload)::encode_result;

            prepare_result prepare(const T &value, const serialization_context &context) {
                if constexpr (auto status = payload.prepare(value, context); std::same_as<decltype(status), error>) {
                    if (status) { return status; }
                }
                if constexpr (static_extent == std::dynamic_extent) {
                    size = payload.size;
                }
                return {};
            }

            encode_result encode(std::span<std::byte, static_extent> destination, const T &value) const {
                if constexpr (tree.node(node).kind == schema_kind::leaf) {
                    constexpr auto type_hash_value = tree.node(node).wire_type;
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
            constexpr auto type_hash_value = tree.node(node).wire_type;
            if constexpr (tree.node(node).kind == schema_kind::leaf) {
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
