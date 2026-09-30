#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <meta>
#include <span>
#include "base.hpp"
#include "../detail/reflection.hpp"

namespace srl {
    template<typename T>
    consteval bool atomic_uses_direct_object_representation() {
        using value_type = codec_value_type<T>;
        if constexpr (!atomic_serializable<value_type>) {
            return false;
        } else if constexpr (requires {atomic_codec<value_type>::uses_direct_object_representation;}) {
            return atomic_codec<value_type>::uses_direct_object_representation;
        } else {
            return false;
        }
    }

    template<typename T>
        requires (detail::auto_atomic_supported<T>())
    struct atomic_codec<T> {
        static constexpr bool uses_direct_object_representation =
            detail::atomic_uses_direct_object_representation_impl<T>();
        static constexpr bool uses_segmented_object_representation =
            detail::atomic_uses_segmented_object_representation_impl<T>();

        static constexpr auto copy_ranges = [] consteval {
            if constexpr (uses_segmented_object_representation) {
                return detail::atomic_copy_ranges<T>();
            } else {
                return std::array<detail::byte_range, 0>{};
            }
        }();

        static constexpr std::size_t fixed_size = [] consteval {
            if constexpr (uses_direct_object_representation) {
                return sizeof(T);
            } else {
                std::size_t result{};
                for (const auto range: copy_ranges) {
                    result += range.size;
                }
                return result;
            }
        }();

        [[nodiscard]] static error encode(
            const T &value,
            std::span<std::byte, fixed_size> destination) {
            if constexpr (uses_direct_object_representation) {
                std::memcpy(destination.data(), &value, sizeof(T));
                return {};
            } else {
                const auto *source = reinterpret_cast<const std::byte *>(&value);
                std::size_t destination_offset{};
                for (const auto range: copy_ranges) {
                    std::memcpy(destination.data() + destination_offset,
                                source + range.offset,
                                range.size);
                    destination_offset += range.size;
                }
                return {};
            }
        }

        [[nodiscard]] static error decode(
            std::span<const std::byte, fixed_size> source,
            T &out) {
            if constexpr (uses_direct_object_representation) {
                std::memcpy(&out, source.data(), sizeof(T));
                return {};
            } else {
                auto *destination = reinterpret_cast<std::byte *>(&out);
                std::size_t source_offset{};
                for (const auto range: copy_ranges) {
                    std::memcpy(destination + range.offset,
                                source.data() + source_offset,
                                range.size);
                    source_offset += range.size;
                }
                return {};
            }
        }
    };
}
