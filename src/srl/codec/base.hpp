#pragma once

#include "../enum.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <meta>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "../detail/utility.hpp"
#include "../detail/reflection_facts.hpp"

namespace srl {
    consteval type_hash_type type_hash(std::meta::info type) noexcept {
        return detail::hash_string(
            detail::fnv1a32_offset,
            std::meta::display_string_of(recursively_dealias(type)));
    }

    template<typename T>
    using codec_value_type = typename[:std::meta::remove_cvref(^^T):];

    template<typename T>
    struct leaf_codec;

    template<typename T>
    concept primitive = std::meta::is_integral_type(^^T)
                        || std::meta::is_enum_type(^^T)
                        || (std::meta::is_floating_point_type(^^T) && (sizeof(T) == 4 || sizeof(T) == 8));

    template<primitive T>
    struct leaf_codec<T> {
        using wire_type = typename[:std::meta::is_enum_type(^^T) ? std::meta::underlying_type(^^T) : ^^T:];

        static_assert(!std::meta::is_floating_point_type(^^T) || std::numeric_limits<T>::is_iec559,
                      "srl floating codecs require IEC 559 representation");
        static constexpr szt fixed_size = sizeof(wire_type);

        [[nodiscard]] static error encode(
            const T &value,
            std::span<std::byte, fixed_size> destination) noexcept {
            if constexpr (std::meta::is_same_type(^^T, ^^bool)) {
                detail::write_trivial(destination, static_cast<std::uint8_t>(value));
            }else {
                detail::write_trivial(destination, static_cast<wire_type>(value));
            }

            return {};
        }

        [[nodiscard]] static error decode(
            std::span<const std::byte, fixed_size> source,
            T &out) noexcept {
            if constexpr (std::meta::is_same_type(^^T, ^^bool)) {
                const auto value = detail::read_trivial<std::uint8_t>(source);
                if (value > 1) {
                    return detail::fail(error_code::invalid_bool);
                }
                out = value != 0;
            } else if constexpr (std::meta::is_enum_type(^^T)) {
                const auto value = detail::read_trivial<wire_type>(source);
                if (!srl::enum_contains<T>(value)) {
                    return detail::fail(error_code::invalid_enum_value);
                }
                out = static_cast<T>(value);
            } else {
                const auto value = detail::read_trivial<wire_type>(source);
                out = value;
            }
            return {};
        }
    };

    template<typename T>
    concept leaf_serializable = requires {
        typename std::integral_constant<
            std::size_t,
            leaf_codec<codec_value_type<T>>::fixed_size>;
        requires (leaf_codec<codec_value_type<T>>::fixed_size != std::dynamic_extent);
        static_cast<error (*)(
            const codec_value_type<T> &,
            std::span<std::byte, leaf_codec<codec_value_type<T>>::fixed_size>)>(
            &leaf_codec<codec_value_type<T>>::encode);
        static_cast<error (*)(
            std::span<const std::byte, leaf_codec<codec_value_type<T>>::fixed_size>,
            codec_value_type<T> &)>(
            &leaf_codec<codec_value_type<T>>::decode);
    };
}
