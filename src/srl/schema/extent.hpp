#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "diagnostic.hpp"
#include "../types.hpp"
#include "../wire_format.hpp"

namespace srl::detail {
    class extent_overflow : public std::overflow_error {
    public:
        consteval extent_overflow(std::string_view operation, szt left, szt right)
        //TODO use format instead
            : std::overflow_error(std::string("schema extent ") + std::string(operation)
                + ": left=" + diagnostic_integer(left) + ", right=" + diagnostic_integer(right)
                + ", maximum_entry_size=" + diagnostic_integer(maximum_entry_size)) {}
    };
}

namespace srl {
    struct schema_extent {
        // consteval explicit schema_extent(szt value) noexcept : bytes(value) {}

    // public:
        // Public and immutable so schema nodes remain structural types for
        // define_static_array; only the checked factories can create a value.
        szt value;


        [[nodiscard]] static consteval schema_extent fixed(szt bytes) {
            if (bytes > detail::maximum_entry_size) {
                throw detail::extent_overflow("fixed size out of range", bytes, 0);
            }
            return schema_extent(bytes);
        }

        [[nodiscard]] static consteval schema_extent dynamic() noexcept {
            return schema_extent(std::dynamic_extent);
        }

        [[nodiscard]] consteval bool is_dynamic() const noexcept {
            return value == std::dynamic_extent;
        }

        // Convert only at interfaces using the standard span extent convention.
        [[nodiscard]] [[deprecated("use value instead")]] consteval std::size_t span_extent() const noexcept {
            return static_cast<std::size_t>(value);
        }

        [[nodiscard]] friend consteval schema_extent operator+(schema_extent left, schema_extent right) {
            if (left.is_dynamic() || right.is_dynamic()) { return dynamic(); }
            if (right.value > detail::maximum_entry_size - left.value) {
                throw detail::extent_overflow("addition overflow", left.value, right.value);
            }
            return schema_extent(left.value + right.value);
        }

        consteval schema_extent &operator+=(schema_extent right) {
            *this = *this + right;
            return *this;
        }

        [[nodiscard]] consteval schema_extent scaled_by(std::size_t count) const {
            if (count == 0 || value == 0) { return fixed(0); }
            if (is_dynamic()) { return dynamic(); }
            if (count > detail::maximum_entry_size / value) {
                throw detail::extent_overflow("multiplication overflow", value, count);
            }
            return schema_extent(value * count);
        }

        [[nodiscard]] consteval schema_extent scaled_by_dynamic_count() const noexcept {
            return value == 0 ? schema_extent(0) : dynamic();
        }

        friend constexpr bool operator==(schema_extent, schema_extent) noexcept = default;
    };
}
