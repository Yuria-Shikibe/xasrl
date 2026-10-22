#pragma once

#include "../enum.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <meta>
#include <span>
#include <vector>

#include "reflection.hpp"
#include "object_layout_model.hpp"
#include "utility.hpp"

namespace srl::detail {
    consteval void collect_object_fields(std::meta::info type, std::meta::info entity,
                                         szt offset, std::vector<object_field> &fields,
                                         std::vector<std::meta::info> path = {}) {
        if (entity != std::meta::info{} && (path.empty() || path.back() != entity)) {
            path.push_back(entity);
        }
        type = std::meta::remove_cv(type);
        if (auto issue = enum_metadata_issue(type); issue.reason != schema_error::none) {
            reject_schema(issue.reason, path, type, issue.detail);
        }
        if (std::meta::is_union_type(type)) {
            reject_schema(schema_error::invalid_object_representation, path, type, "a union needs a tagged codec");
        }
        if (std::meta::is_array_type(type)) {
            const auto element = std::meta::remove_extent(type);
            const auto stride = std::meta::size_of(element);
            for (szt index = 0; index != std::meta::size_of(type) / stride; ++index) {
                collect_object_fields(element, entity, offset + index * stride, fields, path);
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
                    reject_schema(schema_error::invalid_object_representation, path, child,
                        "object representation does not support bit fields, references or virtual bases");
                }
                collect_object_fields(std::meta::type_of(child), child,
                    offset + std::meta::offset_of(child).bytes, fields, path);
            }
        } else {
            fields.push_back({type, entity, offset, offset, std::meta::size_of(type)});
        }
    }

    consteval object_layout make_object_layout(std::meta::info type, bool segmented = false) {
        std::vector<object_field> fields;
        collect_object_fields(type, {}, 0, fields, {type});
        const auto ranges = segmented ? leaf_copy_ranges(type) : std::span<const byte_range>{};
        szt payload_size{};
        for (auto &field: fields) {
            if (segmented) {
                szt wire_offset{};
                for (const auto range: ranges) {
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
        return {payload_size, stored.data(), stored.size()};
    }

    template<const object_layout &Layout>
    [[nodiscard]] error validate_object_bytes(std::span<const std::byte> bytes) noexcept {

        template for (constexpr object_field field: Layout.fields()) {
            using type = typename[:field.type:];
            constexpr auto offset = field.wire_offset;
            if constexpr (std::meta::is_same_type(^^type, ^^bool)) {
                if (detail::read_trivial<std::uint8_t>(bytes.subspan(offset)) > 1) {
                    return fail(error_code::invalid_bool, std::nullopt, offset);
                }
            } else if constexpr (std::meta::is_enum_type(^^type)) {
                const auto value = detail::read_trivial<typename[:std::meta::underlying_type(^^type):]>(bytes.subspan(offset));
                if (!srl::enum_contains<type>(value)) {
                    return fail(error_code::invalid_enum_value, std::nullopt, offset);
                }
            }
        }
        return {};
    }


    template<const object_layout &Layout, typename T, std::size_t Extent>
        requires (std::meta::is_trivially_copyable_type(^^T))
    void encode_object_bytes(const T &value, std::span<std::byte, Extent> destination) noexcept {
        const auto *source = reinterpret_cast<const std::byte *>(&value);
        if constexpr (Layout.payload_size == Extent && Extent == sizeof(T)) {
            std::memcpy(destination.data(), source, Extent);
        } else {

            // Padding has no value semantics. Canonicalize it without reading it.
            if constexpr (Layout.payload_size != Extent) {
                std::memset(destination.data(), 0, Extent);
            }

            template for (constexpr object_field field: Layout.fields()) {
                // GCC 16 requires extracting integers from reflection-bearing metadata.
                constexpr auto wire_offset = field.wire_offset;
                constexpr auto object_offset = field.object_offset;
                constexpr auto size = field.size;
                std::memcpy(destination.data() + wire_offset, source + object_offset, size);
            }
        }
    }

    template<const object_layout &Layout, typename T, std::size_t Extent>
    [[nodiscard]] error decode_object_bytes(std::span<const std::byte, Extent> source, T &out) noexcept {
        if (auto status = detail::validate_object_bytes<Layout>(source); status) {
            return status;
        }
        auto *destination = reinterpret_cast<std::byte *>(&out);
        if constexpr (Layout.payload_size == Extent && Extent == sizeof(T)) {
            std::memcpy(destination, source.data(), Extent);
        } else {
            template for (constexpr object_field field: Layout.fields()) {
                constexpr auto wire_offset = field.wire_offset;
                constexpr auto object_offset = field.object_offset;
                constexpr auto size = field.size;
                std::memcpy(destination + object_offset, source.data() + wire_offset, size);
            }
        }
        return {};
    }
}
