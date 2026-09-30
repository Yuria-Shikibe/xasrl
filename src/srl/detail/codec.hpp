#pragma once

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
#include <type_traits>
#include <utility>
#include <vector>

#include "../context.hpp"
#include "../schema.hpp"
#include "wire_parse.hpp"

namespace srl::detail {
    inline error locate_error(error failure, id_type id, std::uint32_t offset = 0) noexcept {
        return failure.set_id(id).set_offset(offset);
    }

    template<std::size_t Extent, typename Byte, std::size_t SourceExtent>
    auto payload_span(std::span<Byte, SourceExtent> bytes, std::size_t offset, std::size_t size) {
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
        requires (Tree.node(Node).kind == schema_kind::atomic)
    struct value_codec<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr auto descriptor = Tree.node(Node);
        static constexpr auto static_extent = descriptor.encode_extent;

        struct encode_plan {
            static constexpr auto static_extent = value_codec::static_extent;
            static constexpr auto size = static_extent;

            error prepare(const value_type &, const serialization_context &) { return {}; }

            error encode(std::span<std::byte, static_extent> destination, const value_type &value) const {
                if constexpr (descriptor.representation == atomic_representation::forced) {
                    std::memcpy(destination.data(), &value, static_extent);
                    return {};
                } else {
                    return atomic_codec<value_type>::encode(value, destination);
                }
            }
        };

        template<std::size_t Extent>
        [[nodiscard]] static error decode(
            std::span<const std::byte, Extent> source, value_type &out,
            const serialization_context &, id_type id = 0, std::uint32_t offset = 0) {
            // Validate untrusted lengths before forming the fixed-size codec span.
            constexpr auto extent = descriptor.decode_extent;
            if (source.size() != extent) {
                return fail(error_code::invalid_payload_shape, id, offset);
            }
            if constexpr (descriptor.representation == atomic_representation::forced) {
                std::memcpy(&out, source.data(), extent);
                return {};
            } else {
                auto status = atomic_codec<value_type>::decode(source.template first<extent>(), out);
                return status ? locate_error(status, id, offset) : error{};
            }
        }
    };

    struct no_conversion {};

    template<const schema_tree &Tree, std::size_t Node>
    struct field_codec {
        static constexpr auto descriptor = Tree.node(Node);
        static constexpr auto id = descriptor.id;
        static constexpr auto static_extent = descriptor.encode_extent;
        static constexpr auto entity_kind = descriptor.role == schema_role::base
            ? reflected_entity_kind::base : reflected_entity_kind::member;
        static constexpr auto handler_id = descriptor.conversion.handler_id;
        using memory_type = typename[:descriptor.memory_type:];
        using storage_type = node_value_t<Tree, Node>;

        struct encode_plan {
            static constexpr auto id = field_codec::id;
            static constexpr auto static_extent = field_codec::static_extent;
            [[no_unique_address]] std::conditional_t<descriptor.converted(), std::optional<storage_type>, no_conversion> storage;
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
            const field_view &field, T &out, const serialization_context &context) {
            auto &mem = detail::entity_reference<descriptor.entity, entity_kind>(out);
            if constexpr (descriptor.converted()) {
                storage_type storage{};
                if constexpr (handler_id == nullptr) {
                    if (auto status = value_codec<Tree, Node>::decode(field.bytes, storage, context, id, field.offset); status) {
                        return locate_error(status, id, field.offset);
                    }
                    mem = static_cast<memory_type>(std::move(storage));
                } else {
                    auto &handler = context.template get_handler<memory_type, storage_type>(handler_id);
                    if (auto status = value_codec<Tree, Node>::decode(field.bytes, storage, context, id, field.offset); status) {
                        return locate_error(status, id, field.offset);
                    }
                    if (auto status = handler.srl2mem(std::addressof(mem), std::addressof(storage)); status) {
                        return locate_error(status, id, field.offset);
                    }
                }
            } else {
                if (auto status = value_codec<Tree, Node>::decode(field.bytes, mem, context, id, field.offset); status) {
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
            std::string name = "field_";
            auto ordinal = members.size();
            do {
                name.push_back(static_cast<char>('0' + ordinal % 10));
                ordinal /= 10;
            } while (ordinal != 0);
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
            std::size_t size = static_extent == std::dynamic_extent ? content_offset : static_extent;

            error prepare(const value_type &value, const serialization_context &context) {
                template for (constexpr auto member: members) {
                    auto &field = fields.[:member:];
                    if (auto status = field.prepare(value, context); status) {
                        return locate_error(status, field.id);
                    }
                    if constexpr (static_extent == std::dynamic_extent) {
                        if (field.payload.size > std::numeric_limits<std::uint32_t>::max() - size) {
                            return fail(error_code::entry_too_large, field.id);
                        }
                        size += field.payload.size;
                    }
                }
                return {};
            }

            error encode(std::span<std::byte, static_extent> destination, const value_type &value) const {
                write_trivial(destination, entry_head{
                    .size = static_cast<std::uint32_t>(size),
                    .mapping_count = stream ? 0 : static_cast<std::uint32_t>(field_count),
                    .mapping_align = initial_mapping_alignment,
                    .kind = descriptor.wire_kind,
                    .mapping = descriptor.mapping,
                    .mapping_group_size = descriptor.mapping_group_size,
                    .type_raw_name_hash = descriptor.type_hash,
                });
                std::size_t address{};
                std::size_t index{};
                template for (constexpr auto member: members) {
                    const auto &field = fields.[:member:];
                    if constexpr (!stream) {
                        write_trivial(destination.subspan(entry_head_wire_size + index++ * entry_mapping_wire_size),
                            entry_mapping{.id = field.id, .aligned_address = static_cast<std::uint32_t>(address)});
                    }
                    if (auto status = field.encode(
                        payload_span<field.static_extent>(destination, content_offset + address, field.payload.size), value); status) {
                        return locate_error(status, field.id, static_cast<std::uint32_t>(content_offset + address));
                    }
                    address += field.payload.size;
                }
                return {};
            }
        };

        [[nodiscard]] static error decode(
            std::span<const std::byte> source, value_type &out,
            const serialization_context &context, id_type = 0, std::uint32_t = 0) {
            auto entry = parse_entry(source);
            if (!entry) { return entry.error(); }
            return decode_entry(*entry, out, context);
        }

        [[nodiscard]] static error decode_entry(
            const entry_view &entry, value_type &out,
            const serialization_context &context) {
            constexpr auto wire_kind = descriptor.wire_kind;
            constexpr auto mapping = descriptor.mapping;
            constexpr auto group_size = descriptor.mapping_group_size;
            if (entry.head.kind != wire_kind || entry.head.mapping != mapping
                || entry.head.mapping_group_size != group_size) {
                return fail(error_code::entry_kind_mismatch, 0, 9);
            }
            std::size_t cursor{};
            template for (constexpr auto child: Tree.children(Node)) {
                static constexpr auto field_node = Tree.node(child);
                constexpr auto field_id = field_node.id;
                constexpr auto field_extent = field_node.decode_extent;
                auto field = [&]() -> std::expected<field_view, error> {
                    if constexpr (!stream) {
                        return entry.find(field_id);
                    } else {
                        const auto remaining = entry.content.subspan(cursor);
                        const auto offset = static_cast<std::uint32_t>(entry.content_offset + cursor);
                        std::size_t size{};
                        if constexpr (field_extent != std::dynamic_extent) {
                            size = field_extent;
                        } else {
                            if (remaining.size() < entry_head_wire_size) {
                                return std::unexpected{fail(error_code::truncated_entry_header, 0, offset)};
                            }
                            size = read_trivial<entry_head>(remaining).size;
                            if (size < entry_head_wire_size) {
                                return std::unexpected{fail(error_code::invalid_entry_size, 0, offset)};
                            }
                        }
                        if (size > remaining.size()) {
                            return std::unexpected{fail(error_code::payload_out_of_bounds, 0, offset)};
                        }
                        cursor += size;
                        return field_view{.id = 0, .offset = offset, .bytes = remaining.first(size)};
                    }
                }();
                if (!field) { return field.error(); }
                if (auto status = field_codec<Tree, child>::decode(*field, out, context); status) {
                    return status;
                }
            }
            if constexpr (stream) {
                if (cursor != entry.content.size()) {
                    return fail(error_code::trailing_bytes, 0, static_cast<std::uint32_t>(entry.content_offset + cursor));
                }
            }
            return {};
        }
    };

    template<std::size_t Slot, typename Expected, typename Value, typename Visitor>
    void visit_container_slot(Value &&value, Visitor &&visitor) {
        if constexpr (std::same_as<std::remove_cvref_t<Value>, Expected>) {
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

    template<const schema_tree &Tree, std::size_t Node>
    struct container_slot_plans {
        using value_type = node_value_t<Tree, Node>;
        static constexpr auto static_extent = Tree.node(Node).encode_extent;
        std::vector<typename value_codec<Tree, Node>::encode_plan> plans;

        std::expected<std::size_t, error> prepare(const value_type &value, const serialization_context &context) {
            auto &plan = plans.emplace_back();
            if (auto status = plan.prepare(value, context); status) {
                return std::unexpected{status};
            }
            return plan.size;
        }

        std::size_t size(std::size_t index) const noexcept { return plans[index].size; }

        error encode(std::size_t index, std::span<std::byte, static_extent> destination, const value_type &value) const {
            return plans[index].encode(destination, value);
        }
    };

    template<const schema_tree &Tree, std::size_t Node>
        requires (Tree.node(Node).kind == schema_kind::atomic)
    struct container_slot_plans<Tree, Node> {
        using value_type = node_value_t<Tree, Node>;
        static constexpr auto static_extent = Tree.node(Node).encode_extent;

        std::expected<std::size_t, error> prepare(const value_type &, const serialization_context &) {
            return static_extent;
        }

        constexpr std::size_t size(std::size_t) const noexcept { return static_extent; }

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
            std::size_t size = static_extent == std::dynamic_extent ? entry_head_wire_size : static_extent;
            std::uint32_t slot_count{};

            error prepare(const value_type &value, const serialization_context &context) {
                error status{};
                detail::for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) {
                    if (status) { return; }
                    auto plan_size = slots.[:members[Slot]:].prepare(slot, context);
                    if (!plan_size) {
                        status = detail::locate_error(plan_size.error(), slot_count);
                        return;
                    }
                    if constexpr (static_extent == std::dynamic_extent) {
                        static constexpr std::size_t maximum = std::numeric_limits<std::uint32_t>::max();
                        static constexpr std::size_t mapping_size = entry_mapping_wire_size;
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
                detail::
                write_trivial(destination, entry_head{
                    .size = static_cast<std::uint32_t>(size),
                    .mapping_count = slot_count,
                    .mapping_align = initial_mapping_alignment,
                    .kind = descriptor.wire_kind,
                    .mapping = descriptor.mapping,
                    .mapping_group_size = group_size,
                    .type_raw_name_hash = descriptor.type_hash,
                });
                const auto content_offset = entry_head_wire_size
                    + static_cast<std::size_t>(slot_count) * entry_mapping_wire_size;
                std::size_t index{};
                std::size_t address{};
                error status{};
                detail::for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) {
                    if (status) { return; }
                    const auto &plans = slots.[:members[Slot]:];
                    const auto plan_index = index / group_size;
                    const auto plan_size = plans.size(plan_index);
                    write_trivial(destination.subspan(entry_head_wire_size + index * entry_mapping_wire_size),
                        entry_mapping{.id = static_cast<id_type>(index), .aligned_address = static_cast<std::uint32_t>(address)});
                    status = plans.encode(plan_index,
                        payload_span<plans.static_extent>(destination, content_offset + address, plan_size), slot);
                    if (status) {
                        status = locate_error(status, static_cast<id_type>(index), static_cast<std::uint32_t>(content_offset + address));
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
            const serialization_context &context, id_type = 0, std::uint32_t = 0) {
            auto entry = parse_entry(source);
            if (!entry) { return entry.error(); }
            return decode_entry(*entry, out, context);
        }

        [[nodiscard]] static error decode_entry(
            const entry_view &entry, value_type &out,
            const serialization_context &context) {
            using T = value_type;
            using traits = container_traits<T>;
            constexpr auto group_size = descriptor.mapping_group_size;
            constexpr auto fixed_count = descriptor.element_count;
            constexpr auto duplicate_mode = descriptor.duplicates;
            if (entry.head.kind != entry_kind::container
                || entry.head.mapping != mapping_kind::element
                || entry.head.mapping_group_size != group_size) {
                return fail(error_code::entry_kind_mismatch, 0, 9);
            }

            const auto element_count = entry.head.mapping_count / group_size;
            if constexpr (fixed_count != std::dynamic_extent) {
                if (element_count != fixed_count) {
                    return fail(error_code::fixed_container_size_mismatch, 0, 4);
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
                return value_codec<Tree, Tree.child(Node, Slot)>::decode(
                    slot.bytes,
                    value,
                    context,
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
            std::size_t size{};

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
                if constexpr (tree.node(node).kind == schema_kind::atomic) {
                    constexpr auto type_hash_value = tree.node(node).type_hash;
                    write_trivial(destination, entry_head{
                        .size = static_cast<std::uint32_t>(size),
                        .mapping_count = 0,
                        .mapping_align = initial_mapping_alignment,
                        .kind = entry_kind::atomic,
                        .mapping = mapping_kind::none,
                        .mapping_group_size = 0,
                        .type_raw_name_hash = type_hash_value,
                    });
                    return payload.encode(destination.template subspan<entry_head_wire_size>(), value);
                } else {
                    return payload.encode(destination, value);
                }
            }
        };

        [[nodiscard]] static error decode(
            std::span<const std::byte> bytes, T &out, const serialization_context &context) {
            auto entry = detail::parse_entry(bytes);
            if (!entry) {
                return entry.error();
            }
            constexpr auto type_hash_value = tree.node(node).type_hash;
            if constexpr (tree.node(node).kind == schema_kind::atomic) {
                if (entry->head.kind != entry_kind::atomic
                    || entry->head.mapping != mapping_kind::none
                    || entry->head.mapping_group_size != 0) {
                    return detail::fail(error_code::entry_kind_mismatch, 0, 9);
                }
                if (entry->head.type_raw_name_hash != type_hash_value) {
                    return detail::fail(error_code::type_mismatch);
                }
                return value_codec<tree, node>::decode(entry->content, out, context, 0, 0);
            } else {
                return value_codec<tree, node>::decode_entry(*entry, out, context);
            }
        }
    };
}
