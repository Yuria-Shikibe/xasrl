#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <meta>
#include <span>
#include <type_traits>
#include <vector>

#include "reflection.hpp"

namespace srl::detail {
    struct object_field {
        std::meta::info type{};
        std::meta::info entity{};
        szt object_offset{};
        szt wire_offset{};
        szt size{};
    };

    struct object_layout {
        bool valid = true;
        szt payload_size{};
        const object_field *field_data{};
        std::size_t field_count{};

        [[nodiscard]] consteval std::span<const object_field> fields() const noexcept {
            return {field_data, field_count};
        }
    };

    consteval bool collect_object_fields(std::meta::info type, std::meta::info entity,
                                         szt offset, std::vector<object_field> &fields) {
        type = std::meta::remove_cv(type);
        if (std::meta::is_union_type(type)) {
            return false; // No active member is encoded; a union needs a tagged codec.
        }
        if (std::meta::is_array_type(type)) {
            const auto element = std::meta::remove_extent(type);
            const auto stride = std::meta::size_of(element);
            for (szt index = 0; index != std::meta::size_of(type) / stride; ++index) {
                if (!collect_object_fields(element, entity, offset + index * stride, fields)) {
                    return false;
                }
            }
        } else if (std::meta::is_class_type(type)) {
            auto entities = std::meta::bases_of(type, std::meta::access_context::unchecked());
            for (auto member: std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) {
                entities.push_back(member);
            }
            for (auto child: entities) {
                if (!std::meta::annotations_of_with_type(child, ^^tag::transient).empty()) {
                    continue;
                }
                if (std::meta::is_bit_field(child) || std::meta::is_reference_type(std::meta::type_of(child))
                    || (std::meta::is_base(child) && std::meta::is_virtual(child))) {
                    return false;
                }
                if (!collect_object_fields(std::meta::type_of(child), child,
                    offset + std::meta::offset_of(child).bytes, fields)) {
                    return false;
                }
            }
        } else {
            fields.push_back({type, entity, offset, offset, std::meta::size_of(type)});
        }
        return true;
    }

    template<typename T, bool Segmented = false>
    consteval object_layout make_object_layout() {
        std::vector<object_field> fields;
        const bool valid = collect_object_fields(^^T, {}, 0, fields);
        szt payload_size{};
        for (auto &field: fields) {
            if constexpr (Segmented) {
                szt wire_offset{};
                for (const auto range: atomic_copy_ranges<T>()) {
                    if (field.object_offset >= range.offset
                        && field.object_offset + field.size <= range.offset + range.size) {
                        field.wire_offset = wire_offset + field.object_offset - range.offset;
                        break;
                    }
                    wire_offset += range.size;
                }
            }
            payload_size += field.size;
        }
        const auto stored = std::define_static_array(fields);
        return {valid, payload_size, stored.data(), stored.size()};
    }

    template<const object_layout &Layout>
    [[nodiscard]] error validate_object_bytes(std::span<const std::byte> bytes) noexcept {
        template for (constexpr auto field: Layout.fields()) {
            using type = typename[:field.type:];
            constexpr auto offset = field.wire_offset;
            if constexpr (std::same_as<type, bool>) {
                if (read_trivial<std::uint8_t>(bytes.subspan(offset)) > 1) {
                    return fail(error_code::invalid_bool, std::nullopt, offset);
                }
            } else if constexpr (std::is_enum_v<type>) {
                const auto value = read_trivial<std::underlying_type_t<type>>(bytes.subspan(offset));
                if (!enum_contains<type>(value)) {
                    return fail(error_code::invalid_enum_value, std::nullopt, offset);
                }
            }
        }
        return {};
    }

    template<const object_layout &Layout, typename T, std::size_t Extent>
    void encode_object_bytes(const T &value, std::span<std::byte, Extent> destination) noexcept {
        const auto *source = reinterpret_cast<const std::byte *>(&value);
        if constexpr (Layout.payload_size == Extent && Extent == sizeof(T)) {
            std::memcpy(destination.data(), source, Extent);
        } else {
            // Padding has no value semantics. Canonicalize it without reading it.
            if constexpr (Layout.payload_size != Extent) {
                std::memset(destination.data(), 0, Extent);
            }
            template for (constexpr auto field: Layout.fields()) {
                constexpr auto wire_offset = field.wire_offset;
                constexpr auto object_offset = field.object_offset;
                constexpr auto size = field.size;
                std::memcpy(destination.data() + wire_offset, source + object_offset, size);
            }
        }
    }

    template<const object_layout &Layout, typename T, std::size_t Extent>
    [[nodiscard]] error decode_object_bytes(std::span<const std::byte, Extent> source, T &out) noexcept {
        if (auto status = validate_object_bytes<Layout>(source); status) {
            return status;
        }
        auto *destination = reinterpret_cast<std::byte *>(&out);
        if constexpr (Layout.payload_size == Extent && Extent == sizeof(T)) {
            std::memcpy(destination, source.data(), Extent);
        } else {
            template for (constexpr auto field: Layout.fields()) {
                constexpr auto wire_offset = field.wire_offset;
                constexpr auto object_offset = field.object_offset;
                constexpr auto size = field.size;
                std::memcpy(destination + object_offset, source.data() + wire_offset, size);
            }
        }
        return {};
    }
}
