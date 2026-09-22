#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <meta>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

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

    namespace detail {
        template<primitive T>
        struct primitive_wire_type {
            using type = T;
        };

        template<>
        struct primitive_wire_type<bool> {
            using type = std::uint8_t;
        };

        template<primitive T>
            requires std::is_enum_v<T>
        struct primitive_wire_type<T> {
            using type = std::underlying_type_t<T>;
        };

        template<primitive T>
        using primitive_wire_type_t = typename primitive_wire_type<T>::type;
    }

    template<primitive T>
    struct atomic_codec<T> {
        using wire_type = detail::primitive_wire_type_t<T>;

        static_assert(!std::floating_point<T> || std::numeric_limits<T>::is_iec559,
                      "srl floating codecs require IEC 559 representation");
        static constexpr std::size_t fixed_size = sizeof(wire_type);

        [[nodiscard]] static constexpr std::expected<std::size_t, error> encoded_size(
            const T &) noexcept {
            return fixed_size;
        }

        [[nodiscard]] static std::expected<void, error> encode(
            const T &value,
            std::span<std::byte> destination) noexcept {
            if (destination.size() != fixed_size) {
                return detail::fail(error_code::invalid_payload_shape);
            }
            if constexpr (std::same_as<T, bool>) {
                detail::write_trivial(destination, static_cast<wire_type>(value ? 1 : 0));
            } else if constexpr (std::is_enum_v<T>) {
                detail::write_trivial(destination, static_cast<wire_type>(value));
            } else {
                detail::write_trivial(destination, value);
            }
            return {};
        }

        [[nodiscard]] static std::expected<void, error> decode(
            std::span<const std::byte> source,
            T &out) noexcept {
            if (source.size() != fixed_size) {
                return detail::fail(error_code::invalid_payload_shape);
            }
            const auto value = detail::read_trivial<wire_type>(source);
            if constexpr (std::same_as<T, bool>) {
                if (value > 1) {
                    return detail::fail(error_code::invalid_bool);
                }
                out = value != 0;
            } else if constexpr (std::is_enum_v<T>) {
                if (!srl::enum_contains<T>(value)) {
                    return detail::fail(error_code::invalid_enum_value);
                }
                out = static_cast<T>(value);
            } else {
                out = value;
            }
            return {};
        }
    };

    template<>
    struct atomic_codec<std::string> {
        static constexpr std::size_t fixed_size = std::dynamic_extent;

        [[nodiscard]] static std::expected<std::size_t, error> encoded_size(
            const std::string &value) noexcept {
            return value.size();
        }

        [[nodiscard]] static std::expected<void, error> encode(
            const std::string &value,
            std::span<std::byte> destination) noexcept {
            if (destination.size() != value.size()) {
                return detail::fail(error_code::invalid_payload_shape);
            }
            const auto source = std::as_bytes(std::span{value.data(), value.size()});
            std::ranges::copy(source, destination.begin());
            return {};
        }

        [[nodiscard]] static std::expected<void, error> decode(
            std::span<const std::byte> source,
            std::string &out) {
            out.assign(reinterpret_cast<const char *>(source.data()), source.size());
            return {};
        }
    };

    template<typename T>
    concept atomic_serializable = requires(const codec_value_type<T> &value,
                                           codec_value_type<T> &out,
                                           std::span<std::byte> destination,
                                           std::span<const std::byte> source) {
        { atomic_codec<codec_value_type<T>>::fixed_size } -> std::convertible_to<std::size_t>;
        { atomic_codec<codec_value_type<T>>::encoded_size(value) }
            -> std::same_as<std::expected<std::size_t, error>>;
        { atomic_codec<codec_value_type<T>>::encode(value, destination) }
            -> std::same_as<std::expected<void, error>>;
        { atomic_codec<codec_value_type<T>>::decode(source, out) }
            -> std::same_as<std::expected<void, error>>;
    };
}

