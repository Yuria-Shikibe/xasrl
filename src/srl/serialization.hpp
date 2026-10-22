#pragma once

#include <meta>

#include <array>
#include <concepts>
#include <cstddef>
#include <expected>
#include <ranges>
#include <span>
#include <utility>

#include "detail/codec.hpp"
#include "transaction.hpp"
#include "storage/byte_buffer.hpp"

namespace srl {
    template<serializable T>
        requires (schema<T>::node(schema<T>::root).encode_extent != std::dynamic_extent)
    [[nodiscard]] consteval szt serialized_size() noexcept {
        return schema<T>::node(schema<T>::root).encode_extent;
    }

    template<serializable T>
        requires (schema<T>::node(schema<T>::root).encode_extent != std::dynamic_extent)
    [[nodiscard]] constexpr szt serialized_size(
        const T &, const serialization_context &) noexcept {
        return serialized_size<T>();
    }

    template<serializable T>
        requires (schema<T>::node(schema<T>::root).encode_extent == std::dynamic_extent)
    [[nodiscard]] std::expected<szt, error> serialized_size(
        const T &value, const serialization_context &context) {
        typename detail::root_codec<T>::encode_plan plan;
        if (auto status = plan.prepare(value, context); status) {
            return std::unexpected{status};
        }
        return plan.size;
    }

    template<serializable T, std::size_t Extent>
        requires (Extent == std::dynamic_extent
                  || schema<T>::node(schema<T>::root).encode_extent == std::dynamic_extent
                  || Extent == schema<T>::node(schema<T>::root).encode_extent)
    [[nodiscard]] error serialize(
        const T &value,
        std::span<std::byte, Extent> destination,
        const serialization_context &context) {
        typename detail::root_codec<T>::encode_plan plan;
        if (auto status = plan.prepare(value, context); status) {
            return status;
        }
        if constexpr (Extent == std::dynamic_extent || plan.static_extent == std::dynamic_extent) {
            if (destination.size() != plan.size) {
                return detail::fail(error_code::invalid_entry_size);
            }
        }
        return plan.encode(detail::payload_span<plan.static_extent>(destination, 0, plan.size), value);
    }

    template<serializable T>
    [[nodiscard]] error serialize(
        const T &value, byte_buffer &destination, const serialization_context &context) {
        typename detail::root_codec<T>::encode_plan plan;
        if (auto status = plan.prepare(value, context); status) {
            return status;
        }
        if (plan.size > destination.max_size()) {
            return detail::fail(error_code::entry_too_large);
        }
        destination.resize(static_cast<std::size_t>(plan.size));
        return plan.encode(detail::payload_span<plan.static_extent>(destination.span(), 0, plan.size), value);
    }

    template<serializable T, typename Byte, std::size_t Extent>
        requires (std::meta::is_same_type(^^Byte, ^^std::byte) || std::meta::is_same_type(^^Byte, ^^const std::byte))
                 && (!std::meta::is_const_type(^^T) && !std::meta::is_volatile_type(^^T))
                 && transactional<codec_value_type<T>>
    [[nodiscard]] error deserialize(
        std::span<Byte, Extent> bytes,
        T &out,
        const serialization_context &context) {
        if (bytes.size() > context.config.limits.max_input_bytes) {
            return detail::fail(error_code::resource_limit_exceeded);
        }
        auto entry = detail::parse_entry(bytes);
        if (!entry) { return entry.error(); }
        detail::decode_budget budget{context.config.limits};
        auto temporary = transaction_traits<codec_value_type<T>>::clone(out);
        if (auto decoded = detail::root_codec<T>::decode_entry(*entry, temporary, context, budget); decoded) {
            return decoded;
        }
        transaction_traits<codec_value_type<T>>::commit(out, temporary);
        return {};
    }

    template<typename T, std::size_t Size, typename Byte, std::size_t Extent>
        requires serializable<T[Size]>
                 && (std::meta::is_same_type(^^Byte, ^^std::byte) || std::meta::is_same_type(^^Byte, ^^const std::byte))
                 && (!std::meta::is_const_type(^^T) && !std::meta::is_volatile_type(^^T))
                 && transactional<T>
    [[nodiscard]] error deserialize(
        std::span<Byte, Extent> bytes,
        T (&out)[Size],
        const serialization_context &context) {
        if (bytes.size() > context.config.limits.max_input_bytes) {
            return detail::fail(error_code::resource_limit_exceeded);
        }
        auto entry = detail::parse_entry(bytes);
        if (!entry) { return entry.error(); }
        detail::decode_budget budget{context.config.limits};
        auto temporary = [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return std::array<T, Size>{transaction_traits<T>::clone(out[Indices])...};
        }(std::make_index_sequence<Size>{});
        if (auto decoded = detail::root_codec<decltype(temporary)>::decode_entry(*entry, temporary, context, budget); decoded) {
            return decoded;
        }
        for (std::size_t index = 0; index != Size; ++index) {
            transaction_traits<T>::commit(out[index], temporary[index]);
        }
        return {};
    }
}
