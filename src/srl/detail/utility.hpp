#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <memory>
#include <meta>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "../types.hpp"

namespace srl {
    //TODO add support for enum bit flag check, hint flag by if operator & or | is enabled, or explicitly declared.


    template<typename Enum>
        requires std::is_enum_v<Enum>
    [[nodiscard]] constexpr std::string_view enum_name(Enum value) noexcept {
        static constexpr auto enumerators = std::define_static_array(
            std::meta::enumerators_of(^^Enum));
        template for (constexpr auto enumerator: enumerators) {
            if (value == std::meta::extract<Enum>(std::meta::constant_of(enumerator))) {
                return std::meta::identifier_of(enumerator);
            }
        }
        return {};
    }

    template<typename Enum>
        requires std::is_enum_v<Enum>
    [[nodiscard]] constexpr bool enum_contains(std::underlying_type_t<Enum> value) noexcept {
        //TODO optimize for contiguous enum(range bound check) or dense enum(bit set check)
        static constexpr auto enumerators = std::define_static_array(
            std::meta::enumerators_of(^^Enum));
        template for (constexpr auto enumerator: enumerators) {
            constexpr auto reflected = std::meta::extract<Enum>(std::meta::constant_of(enumerator));
            if (std::to_underlying(reflected) == value) {
                return true;
            }
        }
        return false;
    }

    template<typename Enum>
        requires std::is_enum_v<Enum>
    [[nodiscard]] constexpr bool enum_contains(Enum value) noexcept {
        return srl::enum_contains<Enum>(std::to_underlying(value));
    }

    [[nodiscard]] constexpr std::string_view message(error_code code) noexcept {
        const auto name = enum_name(code);
        return name.empty() ? std::string_view{"unknown_error_code"} : name;
    }

    namespace detail {
        constexpr std::uint64_t storage_magic = 0x31304e49424c5253ull;
        constexpr std::uint32_t storage_version = 4;
        constexpr std::uint32_t compression_none = 0;
        constexpr szt storage_head_wire_size = sizeof(storage_head);
        constexpr szt entry_head_wire_size = sizeof(entry_head);
        constexpr szt entry_mapping_wire_size = sizeof(entry_mapping);
        // The all-ones extent is reserved by std::span for dynamic_extent.
        static_assert(sizeof(std::size_t) <= sizeof(szt));
        constexpr szt maximum_entry_size = std::numeric_limits<std::size_t>::max() - 1;
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
            requires std::is_trivially_copyable_v<T>
        void write_trivial(std::span<std::byte> destination,
                           const T &value) noexcept {
            std::memcpy(destination.data(), &value, sizeof(T));
        }

        template<typename T>
            requires std::is_trivially_copyable_v<T>
        [[nodiscard]] T read_trivial(std::span<const std::byte> source) noexcept {
            if constexpr (std::default_initializable<T>) {
                T value{};
                std::memcpy(std::addressof(value), source.data(), sizeof(T));
                return value;
            } else if constexpr (std::is_trivially_destructible_v<T>) {
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
