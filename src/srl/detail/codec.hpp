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

#include "../context.hpp"
#include "../schema.hpp"
#include "wire_parse.hpp"

namespace srl::detail {
    inline error locate_error(error failure, std::optional<id_type> id, szt offset = 0) noexcept {
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
    using node_value_t = typename[:Tree.node(Node).type:];

    template<const schema_tree &Tree, std::size_t Node>
    struct value_codec;

    template<const schema_tree &Tree, std::size_t Node>
        requires (Tree.node(Node).kind == schema_kind::leaf)
    struct value_codec<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr auto descriptor = Tree.node(Node);
        static constexpr auto static_extent = descriptor.encode_extent;
        static constexpr auto object = descriptor.object;

        struct encode_plan {
            static constexpr auto static_extent = value_codec::static_extent;
            static constexpr auto size = static_extent;

            error prepare(const value_type &, const serialization_context &) { return {}; }

            error encode(std::span<std::byte, static_extent> destination, const value_type &value) const {
                if constexpr (descriptor.representation == leaf_representation::forced) {
                    encode_object_bytes<object>(value, destination);
                    return {};
                } else {
                    return leaf_codec<value_type>::encode(value, destination);
                }
            }
        };

        template<std::size_t Extent>
        [[nodiscard]] static error decode(
            std::span<const std::byte, Extent> source, value_type &out,
            const serialization_context &, decode_budget &, std::optional<id_type> id = std::nullopt, szt offset = 0) {
            // Validate untrusted lengths before forming the fixed-size codec span.
            constexpr auto extent = descriptor.decode_extent;
            if (source.size() != extent) {
                return fail(error_code::invalid_payload_shape, id, offset);
            }
            if constexpr (descriptor.representation == leaf_representation::forced) {
                auto status = decode_object_bytes<object>(source.template first<extent>(), out);
                return status ? locate_error(status, id, offset) : error{};
            } else {
                auto status = leaf_codec<value_type>::decode(source.template first<extent>(), out);
                return status ? locate_error(status, id, offset) : error{};
            }
        }
    };

    struct no_conversion {};

    template<const schema_tree &Tree, std::size_t Node>
    struct field_codec {
        static constexpr auto descriptor = Tree.node(Node);
        static constexpr auto id = descriptor.id;
        static constexpr std::optional<id_type> diagnostic_id = descriptor.has_field_id
            ? std::optional<id_type>{id} : std::nullopt;
        static constexpr auto static_extent = descriptor.encode_extent;
        static constexpr auto entity_kind = descriptor.role == schema_role::base
            ? reflected_entity_kind::base : reflected_entity_kind::member;
        static constexpr auto handler_id = descriptor.conversion.handler_id;
        using memory_type = typename[:descriptor.memory_type:];
        using storage_type = node_value_t<Tree, Node>;

        struct encode_plan {
            static constexpr auto id = field_codec::id;
            static constexpr auto diagnostic_id = field_codec::diagnostic_id;
            static constexpr auto wire_type = descriptor.wire_type;
            static constexpr auto static_extent = field_codec::static_extent;
            [[no_unique_address]] typename[:descriptor.converted() ? ^^std::optional<storage_type> : ^^no_conversion:] storage;
            [[no_unique_address]] typename value_codec<Tree, Node>::encode_plan payload;

            template<typename T>
            error prepare(const T &object, const serialization_context &context) {
                const auto &mem = detail::entity_reference<descriptor.entity, entity_kind>(object);
                if constexpr (descriptor.converted()) {
                    if constexpr (handler_id == nullptr) {
                        storage.emplace(static_cast<storage_type>(mem));
                    } else {
                        auto &handler = context.template get_handler<memory_type, storage_type>(handler_id);
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
            error encode(std::span<std::byte, static_extent> destination, const T &object) const {
                if constexpr (descriptor.converted()) {
                    return payload.encode(destination, *storage);
                } else {
                    return payload.encode(destination, detail::entity_reference<descriptor.entity, entity_kind>(object));
                }
            }
        };

        template<typename T>
        [[nodiscard]] static error decode(
            const field_view &field, T &out, const serialization_context &context, decode_budget &budget) {
            if (field.wire_type != descriptor.wire_type) {
                return fail(error_code::type_mismatch, diagnostic_id, field.offset);
            }
            auto &mem = detail::entity_reference<descriptor.entity, entity_kind>(out);
            if constexpr (descriptor.converted()) {
                storage_type storage{};
                if constexpr (handler_id == nullptr) {
                    if (auto status = value_codec<Tree, Node>::decode(field.bytes, storage, context, budget, diagnostic_id, field.offset); status) {
                        return status;
                    }
                    mem = static_cast<memory_type>(std::move(storage));
                } else {
                    auto &handler = context.template get_handler<memory_type, storage_type>(handler_id);
                    if (auto status = value_codec<Tree, Node>::decode(field.bytes, storage, context, budget, diagnostic_id, field.offset); status) {
                        return status;
                    }
                    if (auto status = handler.srl2mem(std::addressof(mem), std::addressof(storage)); status) {
                        return locate_error(status, diagnostic_id, field.offset);
                    }
                }
            } else {
                if (auto status = value_codec<Tree, Node>::decode(field.bytes, mem, context, budget, diagnostic_id, field.offset); status) {
                    return status;
                }
            }
            return {};
        }
    };

    template<const schema_tree &Tree, std::size_t Node>
    using field_encode_plan = typename field_codec<Tree, Node>::encode_plan;

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
        static constexpr auto descriptor = Tree.node(Node);
        static constexpr auto static_extent = descriptor.encode_extent;
        static constexpr bool stream = descriptor.kind == schema_kind::stream;
        static constexpr auto field_count = descriptor.children_count;
        static constexpr auto content_offset = descriptor.header_size + (stream ? 0 : field_count * entry_mapping_wire_size);
        struct encode_plan {
            struct fields_type;
            consteval {
                define_plan_members(^^fields_type, ^^field_encode_plan, std::meta::reflect_object(Tree), Tree, Node);
            }
            static constexpr auto members = std::define_static_array(
                std::meta::nonstatic_data_members_of(^^fields_type, std::meta::access_context::current()));
            static constexpr auto static_extent = value_codec::static_extent;
            [[no_unique_address]] fields_type fields;
            szt size = static_extent == std::dynamic_extent ? content_offset : static_extent;

            error prepare(const value_type &value, const serialization_context &context) {
                template for (constexpr auto member: members) {
                    auto &field = fields.[:member:];
                    if (auto status = field.prepare(value, context); status) {
                        return locate_error(status, field.diagnostic_id);
                    }
                    if constexpr (static_extent == std::dynamic_extent) {
                        if (field.payload.size > maximum_entry_size - size) {
                            return fail(error_code::entry_too_large, field.id);
                        }
                        size += field.payload.size;
                    }
                }
                return {};
            }

            error encode(std::span<std::byte, static_extent> destination, const value_type &value) const {
                constexpr auto wire_kind = descriptor.wire_kind;
                write_trivial(destination, entry_head{
                    .size = size,
                    .metadata = {stream ? 0 : field_count, wire_kind},
                    .wire_type = descriptor.wire_type,
                });
                szt address{};
                std::size_t index{};
                template for (constexpr auto member: members) {
                    const auto &field = fields.[:member:];
                    if constexpr (!stream) {
                        write_trivial(destination.subspan(entry_head_wire_size + index++ * entry_mapping_wire_size),
                            entry_mapping{.id = field.id, .wire_type = field.wire_type, .offset = address});
                    }
                    if (auto status = field.encode(
                        payload_span<field.static_extent>(destination, content_offset + address, field.payload.size), value); status) {
                        return locate_error(status, field.diagnostic_id, content_offset + address);
                    }
                    address += field.payload.size;
                }
                return {};
            }
        };

        [[nodiscard]] static error decode(
            std::span<const std::byte> source, value_type &out,
            const serialization_context &context, decode_budget &budget, std::optional<id_type> id = std::nullopt, szt offset = 0) {
            auto entry = parse_entry(source);
            auto status = entry ? decode_entry(*entry, out, context, budget) : entry.error();
            return status ? locate_error(status, id, offset) : error{};
        }

        [[nodiscard]] static error decode_entry(
            const entry_view &entry, value_type &out,
            const serialization_context &context, decode_budget &budget) {
            constexpr auto wire_kind = descriptor.wire_kind;
            if (entry.head().kind() != wire_kind) {
                return fail(error_code::entry_kind_mismatch, std::nullopt, 11);
            }
            if (entry.head().wire_type != descriptor.wire_type) {
                return fail(error_code::type_mismatch, std::nullopt, 12);
            }
            szt cursor{};
            template for (constexpr auto child: Tree.children(Node)) {
                static constexpr auto field_node = Tree.node(child);
                constexpr auto field_id = field_node.id;
                constexpr auto field_extent = field_node.decode_extent;
                constexpr auto field_wire_type = field_node.wire_type;
                auto field = [&]() -> std::expected<field_view, error> {
                    if constexpr (!stream) {
                        return entry.find(field_id);
                    } else {
                        const auto remaining = entry.content().subspan(cursor);
                        const auto offset = entry.content_offset() + cursor;
                        szt size{};
                        if constexpr (field_extent != std::dynamic_extent) {
                            size = field_extent;
                        } else {
                            if (remaining.size() < entry_head_wire_size) {
                                return std::unexpected{fail(error_code::truncated_entry_header, std::nullopt, offset)};
                            }
                            size = read_trivial<entry_head>(remaining).size;
                            if (size < entry_head_wire_size) {
                                return std::unexpected{fail(error_code::invalid_entry_size, std::nullopt, offset)};
                            }
                        }
                        if (size > remaining.size()) {
                            return std::unexpected{fail(error_code::payload_out_of_bounds, std::nullopt, offset)};
                        }
                        const auto payload_size = static_cast<std::size_t>(size);
                        cursor += payload_size;
                        return field_view{.id = 0, .wire_type = field_wire_type,
                            .offset = offset, .bytes = remaining.first(payload_size)};
                    }
                }();
                if (!field) {
                    if constexpr (!stream && field_node.fallback_type != std::meta::info{}) {
                        if (field.error().code == error_code::missing_field) {
                            using fallback = typename[:field_node.fallback_type:];
                            constexpr auto kind = field_node.role == schema_role::base
                                ? reflected_entity_kind::base : reflected_entity_kind::member;
                            fallback::apply(entity_reference<field_node.entity, kind>(out), context);
                            continue;
                        }
                    }
                    return field.error();
                }
                if (auto status = field_codec<Tree, child>::decode(*field, out, context, budget); status) {
                    return status;
                }
            }
            if constexpr (stream) {
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
        if (descriptor.kind == schema_kind::container || descriptor.encode_extent == std::dynamic_extent) {
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
        static constexpr auto static_extent = Tree.node(Node).encode_extent;
        static constexpr auto wire_type = Tree.node(Node).wire_type;
        std::vector<typename value_codec<Tree, Node>::encode_plan> plans;

        std::expected<szt, error> prepare(const value_type &value, const serialization_context &context) {
            auto &plan = plans.emplace_back();
            if (auto status = plan.prepare(value, context); status) {
                return std::unexpected{status};
            }
            return plan.size;
        }

        szt size(std::size_t index) const noexcept { return plans[index].size; }

        error encode(std::size_t index, std::span<std::byte, static_extent> destination, const value_type &value) const {
            return plans[index].encode(destination, value);
        }
    };

    template<const schema_tree &Tree, std::size_t Node>
        requires (!encoding_needs_cache(Tree, Node))
    struct container_slot_plans<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr auto static_extent = Tree.node(Node).encode_extent;
        static constexpr auto wire_type = Tree.node(Node).wire_type;

        std::expected<szt, error> prepare(const value_type &, const serialization_context &) {
            return static_extent;
        }

        constexpr szt size(std::size_t) const noexcept { return static_extent; }

        error encode(std::size_t, std::span<std::byte, static_extent> destination, const value_type &value) const {
            return typename value_codec<Tree, Node>::encode_plan{}.encode(destination, value);
        }
    };

    template<const schema_tree &Tree, std::size_t Node>
        requires (Tree.node(Node).kind == schema_kind::container)
    struct value_codec<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr auto descriptor = Tree.node(Node);
        static constexpr auto static_extent = descriptor.encode_extent;

        struct encode_plan {
            struct slots_type;
            consteval {
                define_plan_members(^^slots_type, ^^container_slot_plans, std::meta::reflect_object(Tree), Tree, Node);
            }
            static constexpr auto members = std::define_static_array(
                std::meta::nonstatic_data_members_of(^^slots_type, std::meta::access_context::current()));
            static constexpr auto static_extent = value_codec::static_extent;
            [[no_unique_address]] slots_type slots;
            szt size = static_extent == std::dynamic_extent ? entry_head_wire_size : static_extent;
            std::uint32_t slot_count{};

            error prepare(const value_type &value, const serialization_context &context) {
                error status{};
                detail::for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) {
                    if (status) { return; }
                    if (slot_count == entry_metadata::max_mapping_count) {
                        status = fail(error_code::too_many_mappings, slot_count);
                        return;
                    }
                    auto plan_size = slots.[:members[Slot]:].prepare(slot, context);
                    if (!plan_size) {
                        status = detail::locate_error(plan_size.error(), slot_count);
                        return;
                    }
                    if constexpr (static_extent == std::dynamic_extent) {
                        static constexpr szt maximum = maximum_entry_size;
                        static constexpr szt mapping_size = entry_mapping_wire_size;
                        if (size > maximum - mapping_size) {
                            status = fail(error_code::entry_too_large, slot_count);
                            return;
                        }
                        const auto size_with_mapping = size + mapping_size;
                        if (*plan_size > maximum - size_with_mapping) {
                            status = fail(error_code::entry_too_large, slot_count);
                            return;
                        }
                        size = size_with_mapping + *plan_size;
                    }
                    ++slot_count;
                });
                return status;
            }

            error encode(std::span<std::byte, static_extent> destination, const value_type &value) const {
                constexpr auto group_size = descriptor.mapping_group_size;
                constexpr auto wire_kind = descriptor.wire_kind;
                write_trivial(destination, entry_head{
                    .size = size,
                    .metadata = {slot_count, wire_kind},
                    .wire_type = descriptor.wire_type,
                });
                const auto content_offset = entry_head_wire_size
                    + static_cast<std::size_t>(slot_count) * entry_mapping_wire_size;
                std::size_t index{};
                szt address{};
                error status{};
                detail::for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) {
                    if (status) { return; }
                    const auto &plans = slots.[:members[Slot]:];
                    const auto plan_index = index / group_size;
                    const auto plan_size = plans.size(plan_index);
                    write_trivial(destination.subspan(entry_head_wire_size + index * entry_mapping_wire_size),
                        entry_mapping{.id = static_cast<id_type>(index), .wire_type = plans.wire_type, .offset = address});
                    status = plans.encode(plan_index,
                        payload_span<plans.static_extent>(destination, content_offset + address, plan_size), slot);
                    if (status) {
                        status = locate_error(status, static_cast<id_type>(index), content_offset + address);
                        return;
                    }
                    address += plan_size;
                    ++index;
                });
                return status;
            }
        };

        [[nodiscard]] static error decode(
            std::span<const std::byte> source, value_type &out,
            const serialization_context &context, decode_budget &budget, std::optional<id_type> id = std::nullopt, szt offset = 0) {
            auto entry = parse_entry(source);
            auto status = entry ? decode_entry(*entry, out, context, budget) : entry.error();
            return status ? locate_error(status, id, offset) : error{};
        }

        [[nodiscard]] static error decode_entry(
            const entry_view &entry, value_type &out,
            const serialization_context &context, decode_budget &budget) {
            using T = value_type;
            using traits = container_traits<T>;
            constexpr auto group_size = descriptor.mapping_group_size;
            constexpr auto fixed_count = descriptor.element_count;
            constexpr auto duplicate_mode = descriptor.duplicates;
            if (entry.head().kind() != descriptor.wire_kind) {
                return fail(error_code::entry_kind_mismatch, std::nullopt, 11);
            }
            if (entry.head().wire_type != descriptor.wire_type) {
                return fail(error_code::type_mismatch, std::nullopt, 12);
            }

            const auto element_count = entry.head().mapping_count() / group_size;
            if (!budget.consume(element_count, sizeof(typename traits::element_type))) {
                return fail(error_code::resource_limit_exceeded, std::nullopt, 8);
            }
            if constexpr (fixed_count != std::dynamic_extent) {
                if (element_count != fixed_count) {
                    return fail(error_code::fixed_container_size_mismatch, std::nullopt, 8);
                }
            } else {
                detail::clear_container(out);
                detail::reserve_container(out, element_count);
            }

            const auto duplicates = duplicate_mode == schema_duplicates::inherit
                ? context.config.duplicates
                : (duplicate_mode == schema_duplicates::reject ? duplicate_policy::reject : duplicate_policy::deduplicate);
            auto decode_slot = [&]<std::size_t Slot>(std::size_t index, auto &value) -> error {
                const auto slot = entry.at(index);
                constexpr auto slot_wire_type = Tree.node(Tree.child(Node, Slot)).wire_type;
                if (slot.wire_type != slot_wire_type) {
                    return fail(error_code::type_mismatch, static_cast<id_type>(index), slot.offset);
                }
                return value_codec<Tree, Tree.child(Node, Slot)>::decode(
                    slot.bytes,
                    value,
                    context,
                    budget,
                    static_cast<id_type>(index),
                    slot.offset);
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
        static constexpr auto &tree = schema<T>::tree;
        static constexpr auto node = schema<T>::value;
        static constexpr auto static_extent = tree.node(tree.root).encode_extent;

        struct encode_plan {
            static constexpr auto static_extent = root_codec::static_extent;
            typename value_codec<tree, node>::encode_plan payload;
            szt size{};

            error prepare(const T &value, const serialization_context &context) {
                if (auto status = payload.prepare(value, context); status) { return status; }
                if constexpr (static_extent != std::dynamic_extent) {
                    size = static_extent;
                } else {
                    size = payload.size;
                }
                return {};
            }

            error encode(std::span<std::byte, static_extent> destination, const T &value) const {
                if constexpr (tree.node(node).kind == schema_kind::leaf) {
                    constexpr auto type_hash_value = tree.node(node).wire_type;
                    write_trivial(destination, entry_head{
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
            const entry_view &entry, T &out, const serialization_context &context, decode_budget &budget) {
            constexpr auto type_hash_value = tree.node(node).wire_type;
            if constexpr (tree.node(node).kind == schema_kind::leaf) {
                if (entry.head().kind() != entry_kind::leaf) {
                    return detail::fail(error_code::entry_kind_mismatch, std::nullopt, 11);
                }
                if (entry.head().wire_type != type_hash_value) {
                    return detail::fail(error_code::type_mismatch, std::nullopt, 12);
                }
                return value_codec<tree, node>::decode(entry.content(), out, context, budget, std::nullopt, entry_head_wire_size);
            } else {
                return value_codec<tree, node>::decode_entry(entry, out, context, budget);
            }
        }
    };
}
