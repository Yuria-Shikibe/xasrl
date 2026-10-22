#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <meta>
#include <span>
#include "base.hpp"
#include "../detail/reflection.hpp"
#include "../detail/object_layout.hpp"

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
        static constexpr auto layout = detail::make_object_layout<T, uses_segmented_object_representation>();

        static constexpr auto copy_ranges = [] consteval {
            if constexpr (uses_segmented_object_representation) {
                return detail::atomic_copy_ranges<T>();
            } else {
                return std::array<detail::byte_range, 0>{};
            }
        }();

        static constexpr szt fixed_size = [] consteval -> szt {
            if constexpr (uses_direct_object_representation) {
                return sizeof(T);
            } else {
                szt result{};
                for (const auto range: copy_ranges) {
                    result += range.size;
                }
                return result;
            }
        }();

        [[nodiscard]] static error encode(
            const T &value,
            std::span<std::byte, fixed_size> destination) {
            detail::encode_object_bytes<layout>(value, destination);
            return {};
        }

        [[nodiscard]] static error decode(
            std::span<const std::byte, fixed_size> source,
            T &out) {
            return detail::decode_object_bytes<layout>(source, out);
        }
    };
}
