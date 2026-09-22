#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>

#include "utility.hpp"

namespace srl {
    namespace detail {
        struct field_view {
            id_type id{};
            std::span<const std::byte> bytes{};
            std::uint32_t offset{};
        };

        struct entry_view {
            entry_head head{};
            std::span<const std::byte> mappings{};
            std::span<const std::byte> content{};
            std::uint32_t content_offset{};

            [[nodiscard]] id_type mapping_id(std::size_t index) const noexcept {
                return read_trivial<entry_mapping>(
                    mappings.subspan(index * entry_mapping_wire_size)).id;
            }

            [[nodiscard]] std::uint32_t mapping_address(std::size_t index) const noexcept {
                return read_trivial<entry_mapping>(
                    mappings.subspan(index * entry_mapping_wire_size)).aligned_address;
            }

            [[nodiscard]] std::expected<field_view, error> find(id_type id) const noexcept {
                std::size_t first{};
                std::size_t count = head.mapping_count;
                while (count != 0) {
                    const auto step = count / 2;
                    const auto index = first + step;
                    if (mapping_id(index) < id) {
                        first = index + 1;
                        count -= step + 1;
                    } else {
                        count = step;
                    }
                }
                if (first == head.mapping_count || mapping_id(first) != id) {
                    return fail(error_code::missing_field, id);
                }
                const auto begin = mapping_address(first) * head.mapping_align;
                const auto end = first + 1 == head.mapping_count
                                     ? static_cast<std::uint32_t>(content.size())
                                     : mapping_address(first + 1) * head.mapping_align;
                return field_view{
                    .id = id,
                    .bytes = content.subspan(begin, end - begin),
                    .offset = content_offset + begin,
                };
            }
        };

        [[nodiscard]] inline std::expected<entry_view, error> parse_entry(
            std::span<const std::byte> bytes) {
            if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
                return fail(error_code::entry_too_large);
            }
            if (bytes.size() < entry_head_wire_size) {
                return fail(error_code::truncated_entry_header);
            }

            const auto head = read_trivial<entry_head>(bytes);

            if (head.size > bytes.size()) {
                return fail(error_code::invalid_entry_size, 0, 0);
            }
            if (head.size < bytes.size()) {
                return fail(error_code::trailing_bytes, 0, head.size);
            }
            if (head.size < entry_head_wire_size) {
                return fail(error_code::invalid_entry_size, 0, 0);
            }
            if (head.mapping_align != initial_mapping_alignment) {
                return fail(error_code::unsupported_mapping_alignment, 0, 8);
            }
            const auto classification_valid =
                (head.kind == entry_kind::atomic
                 && head.mapping == mapping_kind::none
                 && head.mapping_group_size == 0
                 && head.mapping_count == 0)
                || (head.kind == entry_kind::record
                    && head.mapping == mapping_kind::field
                    && head.mapping_group_size == 1)
                || (head.kind == entry_kind::container
                    && head.mapping == mapping_kind::element
                    && (head.mapping_group_size == 1 || head.mapping_group_size == 2));
            if (!classification_valid) {
                return fail(error_code::invalid_entry_classification, 0, 9);
            }
            if (head.kind == entry_kind::container
                && head.mapping_count % head.mapping_group_size != 0) {
                return fail(error_code::invalid_container_shape, 0, 4);
            }
            if (head.mapping_count > (head.size - entry_head_wire_size) / entry_mapping_wire_size) {
                return fail(error_code::truncated_mapping_table, 0, entry_head_wire_size);
            }

            const auto mapping_bytes = static_cast<std::size_t>(head.mapping_count) * entry_mapping_wire_size;
            const auto content_offset = static_cast<std::size_t>(entry_head_wire_size) + mapping_bytes;
            if (content_offset > head.size) {
                return fail(error_code::truncated_mapping_table, 0, entry_head_wire_size);
            }
            const auto content_size = head.size - static_cast<std::uint32_t>(content_offset);
            id_type previous_id{};
            std::uint32_t previous_address{};
            for (std::uint32_t index = 0; index != head.mapping_count; ++index) {
                const auto header_offset = entry_head_wire_size + index * entry_mapping_wire_size;
                const auto mapping_entry = read_trivial<entry_mapping>(bytes.subspan(header_offset));
                const auto id = mapping_entry.id;
                const auto aligned_address = mapping_entry.aligned_address;
                if (aligned_address > std::numeric_limits<std::uint32_t>::max() / head.mapping_align) {
                    return fail(error_code::payload_out_of_bounds, id, header_offset + 4);
                }
                const auto byte_offset = aligned_address * head.mapping_align;
                if (byte_offset > content_size) {
                    return fail(error_code::payload_out_of_bounds, id, header_offset + 4);
                }
                if (index != 0 && (previous_id >= id || previous_address > aligned_address)) {
                    return fail(error_code::invalid_mapping_order, id, header_offset);
                }
                if (head.kind == entry_kind::container && id != index) {
                    return fail(error_code::invalid_container_shape, id, header_offset);
                }
                previous_id = id;
                previous_address = aligned_address;
            }

            return entry_view{
                .head = head,
                .mappings = bytes.subspan(entry_head_wire_size, mapping_bytes),
                .content = bytes.subspan(content_offset),
                .content_offset = static_cast<std::uint32_t>(content_offset),
            };
        }
    }
}

