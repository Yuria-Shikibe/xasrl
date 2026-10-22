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
#include "../detail/scalar_representation.hpp"

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

    namespace detail {
        template<typename T>
        struct scalar_codec {
            using wire_type = typename scalar_representation<T>::storage_type;

            static_assert(!std::meta::is_floating_point_type(^^T) || std::numeric_limits<T>::is_iec559,
                          "srl floating codecs require IEC 559 representation");
            static constexpr szt fixed_size = sizeof(wire_type);

            [[nodiscard]] static always_success encode(
                const T &value,
                std::span<std::byte, fixed_size> destination) noexcept {
                detail::write_trivial(destination, static_cast<wire_type>(value));
                return {};
            }

            template<validation_mode Mode>
            [[nodiscard]] static scalar_validation_status<T, Mode> decode_with_validation(
                std::span<const std::byte, fixed_size> source,
                T &out) noexcept {
                const auto value = detail::read_trivial<wire_type>(source);
                if constexpr (auto status = validate_scalar<T, Mode>(value); std::same_as<decltype(status), error>) {
                    if (status) { return status; }
                }
                out = static_cast<T>(value);
                return {};
            }

            [[nodiscard]] static scalar_validation_status<T, validation_for(^^T)> decode(
                std::span<const std::byte, fixed_size> source, T &out) noexcept {
                return decode_with_validation<validation_for(^^T)>(source, out);
            }
        };
    }

    template<primitive T>
    struct leaf_codec<T> : detail::scalar_codec<T> {};

    template<typename T>
    concept leaf_serializable = requires(const codec_value_type<T> &value, codec_value_type<T> &out,
                                        std::span<std::byte, leaf_codec<codec_value_type<T>>::fixed_size> bytes,
                                        std::span<const std::byte, leaf_codec<codec_value_type<T>>::fixed_size> source) {
        typename std::integral_constant<
            std::size_t,
            leaf_codec<codec_value_type<T>>::fixed_size>;
        requires (leaf_codec<codec_value_type<T>>::fixed_size != std::dynamic_extent);
        { leaf_codec<codec_value_type<T>>::encode(value, bytes) } -> codec_status;
        { leaf_codec<codec_value_type<T>>::decode(source, out) } -> codec_status;
        static_cast<decltype(leaf_codec<codec_value_type<T>>::encode(value, bytes)) (*)(
            const codec_value_type<T> &,
            std::span<std::byte, leaf_codec<codec_value_type<T>>::fixed_size>)>(
            &leaf_codec<codec_value_type<T>>::encode);
        static_cast<decltype(leaf_codec<codec_value_type<T>>::decode(source, out)) (*)(
            std::span<const std::byte, leaf_codec<codec_value_type<T>>::fixed_size>,
            codec_value_type<T> &)>(
            &leaf_codec<codec_value_type<T>>::decode);
    };
}
