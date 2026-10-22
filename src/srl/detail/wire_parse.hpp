#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>

#include "utility.hpp"

namespace srl::detail {
    struct field_view {
        id_type id{};
        type_hash_type wire_type{};
        szt offset{};
        std::span<const std::byte> bytes{};
    };

    struct entry_view {
        entry_head head{};
        std::span<const std::byte> mappings{};
        std::span<const std::byte> content{};
        szt content_offset{};

        [[nodiscard]] id_type mapping_id(std::size_t index) const noexcept {
            return read_trivial<entry_mapping>(
                mappings.subspan(index * entry_mapping_wire_size)).id;
        }

        [[nodiscard]] szt mapping_address(std::size_t index) const noexcept {
            return read_trivial<entry_mapping>(
                mappings.subspan(index * entry_mapping_wire_size)).offset;
        }

        [[nodiscard]] field_view at(std::size_t index) const noexcept {
            const auto begin = mapping_address(index);
            const auto end = index + 1 == head.mapping_count()
                                 ? content.size()
                                 : mapping_address(index + 1);
            return field_view{
                .id = mapping_id(index),
                .wire_type = read_trivial<entry_mapping>(mappings.subspan(index * entry_mapping_wire_size)).wire_type,
                .offset = content_offset + begin,
                .bytes = content.subspan(begin, end - begin),
            };
        }

        [[nodiscard]] std::expected<field_view, error> find(id_type id) const noexcept {
            std::size_t first{};
            std::size_t count = head.mapping_count();
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
            if (first == head.mapping_count() || mapping_id(first) != id) {
                return std::unexpected{fail(error_code::missing_field, id)};
            }
            return at(first);
        }
    };

    [[nodiscard]] inline std::expected<entry_view, error> parse_entry(
        std::span<const std::byte> bytes) {
        if (bytes.size() > maximum_entry_size) {
            return std::unexpected{fail(error_code::entry_too_large)};
        }
        if (bytes.size() < entry_head_wire_size) {
            return std::unexpected{fail(error_code::truncated_entry_header)};
        }

        const auto head = read_trivial<entry_head>(bytes);

        if (head.size > bytes.size()) {
            return std::unexpected{fail(error_code::invalid_entry_size, std::nullopt, 0)};
        }
        if (head.size < bytes.size()) {
            return std::unexpected{fail(error_code::trailing_bytes, std::nullopt, head.size)};
        }
        if (head.size < entry_head_wire_size) {
            return std::unexpected{fail(error_code::invalid_entry_size, std::nullopt, 0)};
        }
        const auto count = head.mapping_count();
        const auto kind = head.kind();
        const auto classification_valid =
                ((kind == entry_kind::atomic || kind == entry_kind::stream) && count == 0)
                || kind == entry_kind::record
                || kind == entry_kind::sequence
                || kind == entry_kind::map;
        if (!classification_valid) {
            return std::unexpected{fail(error_code::invalid_entry_classification, std::nullopt, 11)};
        }
        if (kind == entry_kind::map && count % 2 != 0) {
            return std::unexpected{fail(error_code::invalid_container_shape, std::nullopt, 8)};
        }
        if (count > (head.size - entry_head_wire_size) / entry_mapping_wire_size) {
            return std::unexpected{fail(error_code::truncated_mapping_table, std::nullopt, entry_head_wire_size)};
        }

        const auto mapping_bytes = static_cast<std::size_t>(count) * entry_mapping_wire_size;
        const auto content_offset = entry_head_wire_size + mapping_bytes;
        const auto content_size = bytes.size() - content_offset;
        id_type previous_id{};
        szt previous_address{};
        for (std::size_t index = 0; index != count; ++index) {
            const auto header_offset = entry_head_wire_size + index * entry_mapping_wire_size;
            const auto mapping_entry = read_trivial<entry_mapping>(bytes.subspan(header_offset));
            const auto id = mapping_entry.id;
            const auto byte_offset = mapping_entry.offset;
            if (byte_offset > content_size) {
                return std::unexpected{fail(error_code::payload_out_of_bounds, id, header_offset + 8)};
            }
            if (index != 0 && (previous_id >= id || previous_address > byte_offset)) {
                return std::unexpected{fail(error_code::invalid_mapping_order, id, header_offset)};
            }
            if ((kind == entry_kind::sequence || kind == entry_kind::map) && id != index) {
                return std::unexpected{fail(error_code::invalid_container_shape, id, header_offset)};
            }
            previous_id = id;
            previous_address = byte_offset;
        }

        return entry_view{
            .head = head,
            .mappings = bytes.subspan(entry_head_wire_size, mapping_bytes),
            .content = bytes.subspan(content_offset),
            .content_offset = content_offset,
        };
    }
}
