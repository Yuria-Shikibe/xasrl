#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <expected>
#include <ranges>
#include <span>
#include <type_traits>

#include "detail/codec.hpp"

namespace srl {
    template<serializable T>
    [[nodiscard]] std::expected<std::size_t, error> serialized_size(
        const T &value, const serialization_context &context) {
        static_assert(schema_valid<T>(), "invalid serialization schema");
        typename detail::root_codec<T>::encode_plan plan;
        if (auto status = plan.prepare(value, context); status) {
            return std::unexpected{status};
        }
        return plan.size;
    }

    template<serializable T>
    [[nodiscard]] error pack_into(
        std::span<std::byte> destination,
        const T &value,
        const serialization_context &context) {
        static_assert(schema_valid<T>(), "invalid serialization schema");
        typename detail::root_codec<T>::encode_plan plan;
        if (auto status = plan.prepare(value, context); status) {
            return status;
        }
        if (destination.size() != plan.size) {
            return detail::fail(error_code::invalid_entry_size);
        }
        return plan.encode(detail::payload_span<plan.static_extent>(destination, 0, plan.size), value);
    }

    template<serializable T>
    [[nodiscard]] std::expected<byte_buffer, error> pack(
        const T &value, const serialization_context &context) {
        static_assert(schema_valid<T>(), "invalid serialization schema");
        typename detail::root_codec<T>::encode_plan plan;
        if (auto status = plan.prepare(value, context); status) {
            return std::unexpected{status};
        }
        byte_buffer result;
        result.resize(plan.size);
        if (auto status = plan.encode(detail::payload_span<plan.static_extent>(result.span(), 0, plan.size), value); status) {
            return std::unexpected{status};
        }
        return result;
    }

    template<serializable T>
    [[nodiscard]] error decode_root(
        std::span<const std::byte> bytes,
        T &out,
        const serialization_context &context) {
        static_assert(schema_valid<T>(), "invalid serialization schema");
        return detail::root_codec<T>::decode(bytes, out, context);
    }

    template<serializable T>
        requires std::copy_constructible<codec_value_type<T>>
                 && std::is_nothrow_swappable_v<codec_value_type<T>>
    [[nodiscard]] error unpack(
        std::span<const std::byte> bytes,
        T &out,
        const serialization_context &context) {
        codec_value_type<T> temporary = out;
        if (auto decoded = srl::decode_root(bytes, temporary, context); decoded) {
            return decoded;
        }
        std::ranges::swap(out, temporary);
        return {};
    }

    template<typename T, std::size_t Size>
        requires serializable<T[Size]>
                 && std::copy_constructible<T>
                 && std::is_nothrow_swappable_v<T>
    [[nodiscard]] error unpack(
        std::span<const std::byte> bytes,
        T (&out)[Size],
        const serialization_context &context) {
        auto temporary = std::to_array(out);
        if (auto decoded = srl::decode_root(bytes, temporary, context); decoded) {
            return decoded;
        }
        for (std::size_t index = 0; index != Size; ++index) {
            std::ranges::swap(out[index], temporary[index]);
        }
        return {};
    }

    template<typename T>
        requires serializable<T> && std::default_initializable<codec_value_type<T>>
    [[nodiscard]] std::expected<codec_value_type<T>, error> extract(
        std::span<const std::byte> bytes,
        const serialization_context &context) {
        codec_value_type<T> result{};
        if (auto decoded = decode_root(bytes, result, context); decoded) {
            return std::unexpected{decoded};
        }
        return result;
    }
}
