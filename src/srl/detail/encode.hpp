#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <meta>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "../context.hpp"
#include "../schema.hpp"

namespace srl {
    namespace detail {
        template<std::meta::info Entity>
        [[nodiscard]] std::expected<void, error> encode_forced_atomic_field(
            std::span<std::byte> destination,
            const reflected_entity_value_t<Entity> &value) {
            static_assert(field_uses_forced_atomic_representation<Entity>());
            if (destination.size() != sizeof(value)) {
                return fail(error_code::invalid_payload_shape);
            }
            std::memcpy(destination.data(), &value, sizeof(value));
            return {};
        }

        template<std::meta::info Entity>
        [[nodiscard]] std::expected<void, error> decode_forced_atomic_field(
            std::span<const std::byte> source,
            reflected_entity_value_t<Entity> &out) {
            static_assert(field_uses_forced_atomic_representation<Entity>());
            if (source.size() != sizeof(out)) {
                return fail(error_code::invalid_payload_shape);
            }
            std::memcpy(&out, source.data(), sizeof(out));
            return {};
        }

        inline error locate_error(error failure, id_type id, std::uint32_t offset = 0) {
            if (failure.id == 0) {
                failure.id = id;
            }
            if (failure.offset == 0) {
                failure.offset = offset;
            }
            return failure;
        }

        template<typename T>
        struct value_plan;

        template<typename T>
            requires atomic_serializable<T>
        struct value_plan<T> {
            std::size_t size{};

            std::expected<void, error> prepare(const T &value, const serialization_context &) {
                auto measured = atomic_codec<T>::encoded_size(value);
                if (!measured) {
                    return std::unexpected{measured.error()};
                }
                if (*measured > std::numeric_limits<std::uint32_t>::max()) {
                    return fail(error_code::entry_too_large);
                }
                size = *measured;
                return {};
            }

            std::expected<void, error> encode(std::span<std::byte> destination,
                                               const T &value) const {
                return atomic_codec<T>::encode(value, destination);
            }
        };

        template<std::meta::info Entity>
        struct forced_field_plan {
            static constexpr std::size_t size = sizeof(reflected_entity_value_t<Entity>);

            std::expected<void, error> prepare(const reflected_entity_value_t<Entity> &,
                                                const serialization_context &) {
                return {};
            }

            std::expected<void, error> encode(std::span<std::byte> destination,
                                               const reflected_entity_value_t<Entity> &value) const {
                return encode_forced_atomic_field<Entity>(destination, value);
            }
        };

        struct no_conversion {};

        template<std::meta::info Entity, reflected_entity_kind Kind>
        struct field_plan {
            using memory_type = reflected_entity_value_t<Entity>;
            using storage_type = field_storage_t<Entity>;
            static constexpr bool converted = conversion_count<Entity>() == 1;
            static constexpr id_type id = std::meta::extract<tag::field>(
                std::meta::annotations_of_with_type(Entity, ^^tag::field).front()).id;

            [[no_unique_address]] std::conditional_t<
                converted, std::optional<storage_type>, no_conversion> storage;
            std::conditional_t<field_atomic_annotation_count<Entity>() == 1,
                               forced_field_plan<Entity>, value_plan<storage_type>> payload;

            template<typename T>
            std::expected<void, error> prepare(const T &object, const serialization_context &context) {
                const auto &mem = entity_reference<Entity, Kind>(object);
                if constexpr (converted) {
                    constexpr auto options = conversion_options<Entity>();
                    if constexpr (options.handler_id == nullptr) {
                        storage.emplace(static_cast<storage_type>(mem));
                    } else {
                        auto &handler = context.template get_handler<memory_type, storage_type>(
                            options.handler_id);
                        storage.emplace();
                        if (auto status = handler.mem2srl(std::addressof(mem),
                                                          std::addressof(*storage)); !status) {
                            return status;
                        }
                    }
                    return payload.prepare(*storage, context);
                } else {
                    return payload.prepare(mem, context);
                }
            }

            template<typename T>
            std::expected<void, error> encode(std::span<std::byte> destination,
                                               const T &object) const {
                if constexpr (converted) {
                    return payload.encode(destination, *storage);
                } else {
                    return payload.encode(destination, entity_reference<Entity, Kind>(object));
                }
            }
        };

        struct planned_field {
            std::meta::info entity;
            reflected_entity_kind kind;
            id_type id;
        };

        template<typename T>
        consteval auto planned_fields() {
            std::vector<planned_field> fields;
            for_each_serialized_entity<T>(
                [&]<std::meta::info Entity, reflected_entity_kind Kind>(tag::field annotation) {
                    fields.push_back({Entity, Kind, annotation.id});
                });
            std::ranges::sort(fields, {}, &planned_field::id);
            return fields;
        }

        template<typename T>
        inline constexpr auto planned_fields_v = std::define_static_array(planned_fields<T>());

        template<typename T, std::size_t... Index>
        auto record_fields_type(std::index_sequence<Index...>)
            -> std::tuple<field_plan<planned_fields_v<T>[Index].entity,
                                     planned_fields_v<T>[Index].kind>...>;

        template<typename T>
            requires (!atomic_serializable<T> && !container_serializable<T>)
        struct value_plan<T> {
            using fields_type = decltype(record_fields_type<T>(
                std::make_index_sequence<serialized_field_count<T>()>{}));
            fields_type fields;
            std::size_t size = entry_head_wire_size
                               + serialized_field_count<T>() * entry_mapping_wire_size;

            std::expected<void, error> prepare(const T &value, const serialization_context &context) {
                std::expected<void, error> status{};
                auto prepare_field = [&](auto &field) {
                    if (!status) {
                        return;
                    }
                    status = field.prepare(value, context);
                    if (!status) {
                        status = std::unexpected{locate_error(status.error(), field.id)};
                        return;
                    }
                    if (field.payload.size > std::numeric_limits<std::uint32_t>::max() - size) {
                        status = fail(error_code::entry_too_large, field.id);
                        return;
                    }
                    size += field.payload.size;
                };
                std::apply([&](auto &...field) { (prepare_field(field), ...); }, fields);
                return status;
            }

            std::expected<void, error> encode(std::span<std::byte> destination,
                                               const T &value) const {
                constexpr auto type_hash_value = type_hash<T>();
                write_trivial(
                    destination,
                    entry_head{
                        .size = static_cast<std::uint32_t>(size),
                        .mapping_count = serialized_field_count<T>(),
                        .mapping_align = initial_mapping_alignment,
                        .kind = entry_kind::record,
                        .mapping = mapping_kind::field,
                        .mapping_group_size = 1,
                        .type_raw_name_hash = type_hash_value,
                    });
                constexpr auto content_offset = entry_head_wire_size
                                                + serialized_field_count<T>() * entry_mapping_wire_size;
                std::size_t address{};
                std::size_t index{};
                std::expected<void, error> status{};
                auto encode_field = [&](const auto &field) {
                    if (!status) {
                        return;
                    }
                    write_trivial(
                        destination.subspan(entry_head_wire_size
                                            + index++ * entry_mapping_wire_size),
                        entry_mapping{
                            .id = field.id,
                            .aligned_address = static_cast<std::uint32_t>(address),
                        });
                    status = field.encode(destination.subspan(content_offset + address,
                                                               field.payload.size), value);
                    if (!status) {
                        status = std::unexpected{locate_error(
                            status.error(), field.id,
                            static_cast<std::uint32_t>(content_offset + address))};
                        return;
                    }
                    address += field.payload.size;
                };
                std::apply([&](const auto &...field) { (encode_field(field), ...); }, fields);
                return status;
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
                    visit_container_slot<0, typename traits::element_type>(element, visitor);
                } else {
                    if constexpr (requires { traits::key(element); traits::mapped(element); }) {
                        visit_container_slot<0, typename traits::key_type>(traits::key(element), visitor);
                        visit_container_slot<1, typename traits::mapped_type>(traits::mapped(element), visitor);
                    } else {
                        visit_container_slot<0, typename traits::key_type>(element.first, visitor);
                        visit_container_slot<1, typename traits::mapped_type>(element.second, visitor);
                    }
                }
            }
        }

        template<typename T>
        auto container_plans_type() {
            using traits = container_traits<T>;
            if constexpr (traits::mapping_group_size == 1) {
                return std::type_identity<std::tuple<
                    std::vector<value_plan<typename traits::element_type>>>>{};
            } else {
                return std::type_identity<std::tuple<
                    std::vector<value_plan<typename traits::key_type>>,
                    std::vector<value_plan<typename traits::mapped_type>>>>{};
            }
        }

        template<typename T>
            requires (!atomic_serializable<T> && container_serializable<T>)
        struct value_plan<T> {
            typename decltype(container_plans_type<T>())::type slots;
            std::size_t size = entry_head_wire_size;
            std::uint32_t slot_count{};

            std::expected<void, error> prepare(const T &value, const serialization_context &context) {
                std::expected<void, error> status{};
                for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) {
                    if (!status) {
                        return;
                    }
                    auto &plan = std::get<Slot>(slots).emplace_back();
                    status = plan.prepare(slot, context);
                    if (!status) {
                        status = std::unexpected{locate_error(status.error(), slot_count)};
                        return;
                    }
                    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
                    if (size > maximum - entry_mapping_wire_size
                        || plan.size > maximum - entry_mapping_wire_size - size) {
                        status = fail(error_code::entry_too_large, slot_count);
                        return;
                    }
                    size += entry_mapping_wire_size + plan.size;
                    ++slot_count;
                });
                return status;
            }

            std::expected<void, error> encode(std::span<std::byte> destination,
                                               const T &value) const {
                constexpr auto group_size = container_traits<T>::mapping_group_size;
                constexpr auto type_hash_value = type_hash<T>();
                write_trivial(
                    destination,
                    entry_head{
                        .size = static_cast<std::uint32_t>(size),
                        .mapping_count = slot_count,
                        .mapping_align = initial_mapping_alignment,
                        .kind = entry_kind::container,
                        .mapping = mapping_kind::element,
                        .mapping_group_size = group_size,
                        .type_raw_name_hash = type_hash_value,
                    });
                const auto content_offset = entry_head_wire_size
                                            + static_cast<std::size_t>(slot_count) * entry_mapping_wire_size;
                std::size_t index{};
                std::size_t address{};
                std::expected<void, error> status{};
                for_each_container_slot(value, [&]<std::size_t Slot>(const auto &slot) {
                    if (!status) {
                        return;
                    }
                    const auto &plan = std::get<Slot>(slots)[index / group_size];
                    write_trivial(
                        destination.subspan(entry_head_wire_size
                                            + index * entry_mapping_wire_size),
                        entry_mapping{
                            .id = static_cast<id_type>(index),
                            .aligned_address = static_cast<std::uint32_t>(address),
                        });
                    status = plan.encode(destination.subspan(content_offset + address, plan.size), slot);
                    if (!status) {
                        status = std::unexpected{locate_error(
                            status.error(), static_cast<id_type>(index),
                            static_cast<std::uint32_t>(content_offset + address))};
                        return;
                    }
                    address += plan.size;
                    ++index;
                });
                return status;
            }
        };

        template<typename T>
        struct root_plan {
            value_plan<codec_value_type<T>> payload;
            std::size_t size{};

            std::expected<void, error> prepare(const T &value, const serialization_context &context) {
                if (auto status = payload.prepare(value, context); !status) {
                    return status;
                }
                size = payload.size;
                if constexpr (atomic_serializable<T>) {
                    if (size > std::numeric_limits<std::uint32_t>::max() - entry_head_wire_size) {
                        return fail(error_code::entry_too_large);
                    }
                    size += entry_head_wire_size;
                }
                return {};
            }

            std::expected<void, error> encode(std::span<std::byte> destination, const T &value) const {
                if (destination.size() != size) {
                    return fail(error_code::invalid_entry_size);
                }
                if constexpr (atomic_serializable<T>) {
                    constexpr auto type_hash_value = type_hash<codec_value_type<T>>();
                    write_trivial(
                        destination,
                        entry_head{
                            .size = static_cast<std::uint32_t>(size),
                            .mapping_count = 0,
                            .mapping_align = initial_mapping_alignment,
                            .kind = entry_kind::atomic,
                            .mapping = mapping_kind::none,
                            .mapping_group_size = 0,
                            .type_raw_name_hash = type_hash_value,
                        });
                    return payload.encode(destination.subspan(entry_head_wire_size), value);
                } else {
                    return payload.encode(destination, value);
                }
            }
        };
    }
}
