#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <expected>
#include <optional>
#include <span>

#include "../schema/model.hpp"
#include "wire_parse.hpp"

namespace srl::detail {
    consteval std::optional<id_type> diagnostic_id(const schema_node &node) {
        return node.field_id();
    }

    consteval szt record_content_offset(schema_kind kind, std::size_t fields) {
        return entry_head_wire_size + (kind == schema_kind::stream ? 0 : fields * entry_mapping_wire_size);
    }

    inline error validate_entry_type(const entry_view &entry, entry_kind kind, type_hash_type wire_type) {
        if (entry.head().kind() != kind) {
            return fail(error_code::entry_kind_mismatch, std::nullopt, 11);
        }
        if (entry.head().wire_type != wire_type) {
            return fail(error_code::type_mismatch, std::nullopt, 12);
        }
        return {};
    }

    // Commit the accumulated size only after both additions have been checked.
    inline error append_encoded_size(szt &size, szt payload, szt mapping, id_type id) {
        if (size > maximum_entry_size - mapping) {
            return fail(error_code::entry_too_large, id);
        }
        const auto prefix = size + mapping;
        if (payload > maximum_entry_size - prefix) {
            return fail(error_code::entry_too_large, id);
        }
        size = prefix + payload;
        return {};
    }

    // One complete group establishes a checked stride. Only the first group
    // exceeding either wire limit is walked, preserving the failing slot ID.
    inline std::expected<szt, error> measure_fixed_slots(std::size_t count, std::span<const szt> extents) {
        if (count == 0) { return entry_head_wire_size; }
        szt first = entry_head_wire_size;
        for (std::size_t i = 0; i != extents.size(); ++i) {
            if (auto status = append_encoded_size(first, extents[i], entry_mapping_wire_size, static_cast<id_type>(i)); status) {
                return std::unexpected{status};
            }
        }
        const auto stride = first - entry_head_wire_size;
        const auto size_groups = (maximum_entry_size - entry_head_wire_size) / stride;
        const auto mapping_groups = entry_metadata::max_mapping_count / extents.size();
        const auto groups = std::min<szt>(count, std::min<szt>(size_groups, mapping_groups));
        szt size = entry_head_wire_size + groups * stride;
        if (groups == count) { return size; }
        for (std::size_t i = 0; i != extents.size(); ++i) {
            const auto id = static_cast<id_type>(groups * extents.size() + i);
            if (id == entry_metadata::max_mapping_count) {
                return std::unexpected{fail(error_code::too_many_mappings, id)};
            }
            if (auto status = append_encoded_size(size, extents[i], entry_mapping_wire_size, id); status) {
                return std::unexpected{status};
            }
        }
        std::unreachable();
    }

    inline std::expected<field_view, error> next_stream_field(
        const entry_view &entry, szt &cursor, szt extent, type_hash_type wire_type, bool has_header = false) {
        const auto remaining = entry.content().subspan(cursor);
        const auto offset = entry.content_offset() + cursor;
        szt size = extent;
        // A malformed nested header still determines the original diagnostic
        // and slice boundary, even when its valid schema has a fixed width.
        if (extent == std::dynamic_extent || has_header) {
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
        cursor += size;
        return field_view{.id = 0, .wire_type = wire_type,
            .offset = offset, .bytes = remaining.first(static_cast<std::size_t>(size))};
    }

    constexpr duplicate_policy resolve_duplicates(schema_duplicates mode, duplicate_policy inherited) {
        return mode == schema_duplicates::inherit ? inherited
            : (mode == schema_duplicates::reject ? duplicate_policy::reject : duplicate_policy::deduplicate);
    }
}
