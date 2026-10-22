#pragma once

#include <cstddef>
#include <cstring>
#include <meta>
#include <span>
#include "base.hpp"
#include "../detail/reflection.hpp"
#include "../detail/object_layout.hpp"

namespace srl {
    template<typename T>
    consteval bool leaf_uses_direct_object_representation() {
        using value_type = codec_value_type<T>;
        if constexpr (!leaf_serializable<value_type>) {
            return false;
        } else if constexpr (requires {leaf_codec<value_type>::uses_direct_object_representation;}) {
            return leaf_codec<value_type>::uses_direct_object_representation;
        } else {
            return false;
        }
    }

    template<typename T>
        requires (detail::auto_leaf_supported(^^T))
    struct leaf_codec<T> {
        static constexpr bool uses_direct_object_representation =
            detail::leaf_uses_direct_object_representation_impl(^^T);
        static constexpr bool uses_segmented_object_representation =
            detail::leaf_uses_segmented_object_representation_impl(^^T);
        static constexpr detail::object_layout layout = detail::make_object_layout(^^T, uses_segmented_object_representation);

        static constexpr auto copy_ranges = uses_segmented_object_representation
            ? detail::leaf_copy_ranges(^^T) : std::span<const detail::byte_range>{};
        static constexpr szt fixed_size = uses_direct_object_representation
            ? sizeof(T) : detail::byte_ranges_size(copy_ranges);

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
