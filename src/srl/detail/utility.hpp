#pragma once

#include <meta>

#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

#include "../types.hpp"
#include "../wire_format.hpp"

namespace srl {
    namespace detail {
        constexpr type_hash_type fnv1a32_offset = 2166136261u;
        constexpr type_hash_type fnv1a32_prime = 16777619u;

        [[nodiscard]] constexpr error fail(
            error_code code,
            std::optional<id_type> id = std::nullopt,
            szt offset = 0,
            std::optional<std::error_code> system_error = std::nullopt) noexcept {
            return error{
                .code = code,
                .id = id,
                .offset = offset,
                .system_error = system_error,
            };
        }

        template<typename T>
            requires (std::meta::is_trivially_copyable_type(^^T))
        void write_trivial(std::span<std::byte> destination,
                           const T &value) noexcept {
            std::memcpy(destination.data(), &value, sizeof(T));
        }

        template<typename T>
            requires (std::meta::is_trivially_copyable_type(^^T))
        [[nodiscard]] T read_trivial(std::span<const std::byte> source) noexcept {
            if constexpr (std::default_initializable<T>) {
                T value{};
                std::memcpy(std::addressof(value), source.data(), sizeof(T));
                return value;
            } else if constexpr (std::meta::is_trivially_destructible_type(^^T)) {
                union storage {
                    T value;

                    constexpr storage() noexcept {}
                    constexpr ~storage() noexcept {}
                } result;
                std::memcpy(std::addressof(result.value), source.data(), sizeof(T));
                return result.value;
            } else {
                alignas(T) std::array<std::byte, sizeof(T)> representation{};
                std::memcpy(representation.data(), source.data(), sizeof(T));
                return std::bit_cast<T>(representation);
            }
        }

        consteval type_hash_type hash_byte(type_hash_type hash, std::uint8_t value) noexcept {
            return (hash ^ value) * fnv1a32_prime;
        }

        consteval type_hash_type hash_string(type_hash_type hash, std::string_view value) noexcept {
            for (const char character: value) {
                hash = hash_byte(hash, static_cast<std::uint8_t>(character));
            }
            return hash;
        }
    }
}
