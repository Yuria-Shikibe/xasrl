#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>
#include <expected>
#include <optional>
#include <span>

#include "../schema/model.hpp"
#include "wire_document.hpp"

namespace xasrl::detail {
    // A successful measurement and an unchanged source bound all reservations.
    class encoding_output {
        std::span<std::byte> remaining_;
        std::size_t position_{};
        bool schema_only_;

    public:
        explicit encoding_output(std::span<std::byte> destination, bool schema_only) noexcept : remaining_(destination), schema_only_(schema_only) {}

        [[nodiscard]] bool schema_only() const noexcept { return schema_only_; }

        void copy(std::span<const std::byte> bytes) {
            std::memcpy(take(bytes.size()).data(), bytes.data(), bytes.size());
        }

        [[nodiscard]] std::size_t position() const noexcept { return position_; }

        [[nodiscard]] std::span<std::byte> take(std::size_t size) {
            auto bytes = remaining_.first(size);
            remaining_ = remaining_.subspan(size);
            position_ += size;
            return bytes;
        }

        template<std::size_t Size>
        [[nodiscard]] std::span<std::byte, Size> take() {
            return take(Size).template first<Size>();
        }
    };

    consteval std::optional<id_type> diagnostic_id(const schema_node &node) {
        return node.field_id();
    }


    template<typename Entry>
    inline error validate_entry_type(const Entry &entry, entry_kind kind, type_hash_type wire_type) {
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
    inline std::expected<szt, error> measure_fixed_slots(std::size_t count, std::span<const szt> extents, szt prefix = entry_head_wire_size) {
        if (count == 0) { return prefix; }
        szt first = prefix;
        for (std::size_t i = 0; i != extents.size(); ++i) {
            if (auto status = append_encoded_size(first, extents[i], 0, static_cast<id_type>(i)); status) {
                return std::unexpected{status};
            }
        }
        const auto stride = first - prefix;
        const auto size_groups = stride == 0 ? maximum_entry_size : (maximum_entry_size - prefix) / stride;
        const auto mapping_groups = entry_metadata::max_mapping_count / extents.size();
        const auto groups = std::min<szt>(count, std::min<szt>(size_groups, mapping_groups));
        szt size = prefix + groups * stride;
        if (groups == count) { return size; }
        for (std::size_t i = 0; i != extents.size(); ++i) {
            const auto id = static_cast<id_type>(groups * extents.size() + i);
            if (id == entry_metadata::max_mapping_count) {
                return std::unexpected{fail(error_code::too_many_mappings, id)};
            }
            if (auto status = append_encoded_size(size, extents[i], 0, id); status) {
                return std::unexpected{status};
            }
        }
        std::unreachable();
    }


    constexpr duplicate_policy resolve_duplicates(schema_duplicates mode, duplicate_policy inherited) {
        return mode == schema_duplicates::inherit ? inherited
            : (mode == schema_duplicates::reject ? duplicate_policy::reject : duplicate_policy::deduplicate);
    }
}
