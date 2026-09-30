#pragma once

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

#include "../wire.hpp"

namespace srl {
    consteval std::meta::info recursively_dealias(std::meta::info info) {
        if (std::meta::is_type_alias(info)) {
            return recursively_dealias(std::meta::dealias(info));
        }
        return info;
    }

    consteval type_hash_type type_hash(std::meta::info type) noexcept {
        return detail::hash_string(
            detail::fnv1a32_offset,
            std::meta::display_string_of(recursively_dealias(type)));
    }

    template<typename T>
    using codec_value_type = std::remove_cvref_t<T>;

    template<typename T>
    consteval type_hash_type type_hash() noexcept {
        return type_hash(^^T);
    }

    template<typename T>
    struct atomic_codec;

    template<typename T>
    concept primitive = std::integral<T>
                        || std::is_enum_v<T>
                        || (std::floating_point<T> && (sizeof(T) == 4 || sizeof(T) == 8));

    template<primitive T>
    struct atomic_codec<T> {
        using wire_type = T;

        static_assert(!std::floating_point<T> || std::numeric_limits<T>::is_iec559,
                      "srl floating codecs require IEC 559 representation");
        static constexpr std::size_t fixed_size = sizeof(wire_type);

        [[nodiscard]] static error encode(
            const T &value,
            std::span<std::byte, fixed_size> destination) noexcept {
            if constexpr (std::is_same_v<T, bool>) {
                detail::write_trivial(destination, static_cast<std::uint8_t>(value));
            }else {
                detail::write_trivial(destination, static_cast<wire_type>(value));
            }

            return {};
        }

        [[nodiscard]] static error decode(
            std::span<const std::byte, fixed_size> source,
            T &out) noexcept {
            if constexpr (std::same_as<T, bool>) {
                const auto value = detail::read_trivial<std::uint8_t>(source);
                if (value > 1) {
                    return detail::fail(error_code::invalid_bool);
                }
                out = value != 0;
            } else if constexpr (std::is_enum_v<T>) {
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
    concept atomic_serializable = requires {
        typename std::integral_constant<
            std::size_t,
            atomic_codec<codec_value_type<T>>::fixed_size>;
        requires (atomic_codec<codec_value_type<T>>::fixed_size != std::dynamic_extent);
        static_cast<error (*)(
            const codec_value_type<T> &,
            std::span<std::byte, atomic_codec<codec_value_type<T>>::fixed_size>)>(
            &atomic_codec<codec_value_type<T>>::encode);
        static_cast<error (*)(
            std::span<const std::byte, atomic_codec<codec_value_type<T>>::fixed_size>,
            codec_value_type<T> &)>(
            &atomic_codec<codec_value_type<T>>::decode);
    };
}
