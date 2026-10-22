#pragma once

#include <concepts>
#include <cstdint>
#include <meta>
#include <span>

#include "../enum.hpp"
#include "utility.hpp"
#include "validation.hpp"

namespace srl::detail {
    template<typename T>
    struct scalar_representation {
        using storage_type = T;
        using validation_status = always_success;

        static validation_status validate(storage_type) noexcept { return {}; }
    };

    template<>
    struct scalar_representation<bool> {
        static_assert(sizeof(bool) == sizeof(std::uint8_t), "srl requires one-byte bool representation");
        using storage_type = std::uint8_t;
        using validation_status = error;

        static validation_status validate(storage_type value) noexcept {
            return value <= 1 ? error{} : fail(error_code::invalid_bool);
        }
    };

    template<typename T>
        requires (std::meta::is_enum_type(^^T))
    struct scalar_representation<T> {
        using underlying_type = typename[:std::meta::underlying_type(^^T):];
        using underlying = scalar_representation<underlying_type>;
        using storage_type = typename underlying::storage_type;
        using validation_status = error;

        static validation_status validate(storage_type value) noexcept {
            // Validate the representation before converting a bool-backed enum.
            if (underlying::validate(value)) { return fail(error_code::invalid_enum_value); }
            return srl::enum_contains<T>(static_cast<underlying_type>(value))
                ? error{} : fail(error_code::invalid_enum_value);
        }
    };

    template<typename T, validation_mode Mode>
    using scalar_validation_status = status_type<Mode == validation_mode::unchecked
        || std::same_as<typename scalar_representation<T>::validation_status, always_success>>;

    template<typename T, validation_mode Mode>
    scalar_validation_status<T, Mode> validate_scalar(
        typename scalar_representation<T>::storage_type value) noexcept {
        if constexpr (Mode == validation_mode::checked) {
            return scalar_representation<T>::validate(value);
        } else {
            return {};
        }
    }

    template<typename T, validation_mode Mode>
    scalar_validation_status<T, Mode> validate_scalar_bytes(std::span<const std::byte> bytes) noexcept {
        if constexpr (std::same_as<scalar_validation_status<T, Mode>, error>) {
            return detail::validate_scalar<T, Mode>(read_trivial<typename scalar_representation<T>::storage_type>(bytes));
        } else {
            return {};
        }
    }

    template<typename T>
    inline constexpr bool scalar_validation_always_succeeds =
        std::same_as<typename scalar_representation<T>::validation_status, always_success>;

    consteval bool scalar_decode_always_succeeds(std::meta::info type, validation_mode mode) {
        return mode == validation_mode::unchecked || std::meta::extract<bool>(
            std::meta::substitute(^^scalar_validation_always_succeeds, {std::meta::remove_cv(type)}));
    }
}
