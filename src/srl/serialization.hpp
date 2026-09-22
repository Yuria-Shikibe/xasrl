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

#include "detail/decode.hpp"
#include "detail/encode.hpp"

namespace srl {
    template<typename T>
    concept context_free_serializable = serializable<T> && (schema_valid<T>())
                                        && (!requires_context<T>());

    template<serializable T>
    [[nodiscard]] std::expected<std::size_t, error> serialized_size(
        const T &value, const serialization_context &context) {
        static_assert(schema_valid<T>(), "invalid serialization schema");
        detail::root_plan<T> plan;
        if (auto status = plan.prepare(value, context); !status) {
            return std::unexpected{status.error()};
        }
        return plan.size;
    }

    template<serializable T>
    [[nodiscard]] std::expected<void, error> pack_into(
        std::span<std::byte> destination,
        const T &value,
        const serialization_context &context) {
        static_assert(schema_valid<T>(), "invalid serialization schema");
        detail::root_plan<T> plan;
        if (auto status = plan.prepare(value, context); !status) {
            return status;
        }
        return plan.encode(destination, value);
    }

    template<serializable T>
    [[nodiscard]] std::expected<byte_buffer, error> pack(
        const T &value, const serialization_context &context) {
        static_assert(schema_valid<T>(), "invalid serialization schema");
        detail::root_plan<T> plan;
        if (auto status = plan.prepare(value, context); !status) {
            return std::unexpected{status.error()};
        }
        byte_buffer result;
        result.resize(plan.size);
        if (auto status = plan.encode(result.span(), value); !status) {
            return std::unexpected{status.error()};
        }
        return result;
    }

    template<context_free_serializable T>
    [[nodiscard]] std::expected<std::size_t, error> serialized_size(const T &value) {
        return serialized_size(value, serialization_context{});
    }

    template<context_free_serializable T>
    [[nodiscard]] std::expected<void, error> pack_into(
        std::span<std::byte> destination, const T &value) {
        return pack_into(destination, value, serialization_context{});
    }

    template<context_free_serializable T>
    [[nodiscard]] std::expected<byte_buffer, error> pack(const T &value) {
        return pack(value, serialization_context{});
    }

    // Deducing Context keeps legacy read(..., {}) calls bound to decode_options.
    template<serializable T, std::same_as<serialization_context> Context>
    [[nodiscard]] std::expected<void, error> decode_root(
        std::span<const std::byte> bytes,
        T &out,
        const Context &context,
        decode_options options = {}) {
        static_assert(schema_valid<T>(), "invalid serialization schema");
        auto entry = detail::parse_entry(bytes);
        if (!entry) {
            return std::unexpected{entry.error()};
        }
        if constexpr (atomic_serializable<T>) {
            if (entry->head.kind != entry_kind::atomic
                || entry->head.mapping != mapping_kind::none
                || entry->head.mapping_group_size != 0) {
                return detail::fail(error_code::entry_kind_mismatch, 0, 9);
            }
            if (entry->head.type_raw_name_hash != type_hash<codec_value_type<T>>()) {
                return detail::fail(error_code::type_mismatch);
            }
            return detail::decode_value(entry->content, out, context, 0, 0, options);
        } else if constexpr (container_serializable<T>) {
            return detail::decode_container(*entry, out, context, options, std::nullopt);
        } else {
            return detail::decode_record(*entry, out, context, options);
        }
    }

    template<serializable T, std::same_as<serialization_context> Context>
        requires std::copy_constructible<codec_value_type<T>>
                 && std::is_nothrow_swappable_v<codec_value_type<T>>
    [[nodiscard]] std::expected<void, error> unpack(
        std::span<const std::byte> bytes,
        T &out,
        const Context &context,
        decode_options options = {}) {
        codec_value_type<T> temporary = out;
        if (auto decoded = decode_root(bytes, temporary, context, options); !decoded) {
            return decoded;
        }
        using std::swap;
        swap(out, temporary);
        return {};
    }

    template<typename T, std::size_t Size, std::same_as<serialization_context> Context>
        requires serializable<T[Size]>
                 && std::copy_constructible<T>
                 && std::is_nothrow_swappable_v<T>
    [[nodiscard]] std::expected<void, error> unpack(
        std::span<const std::byte> bytes,
        T (&out)[Size],
        const Context &context,
        decode_options options = {}) {
        auto temporary = std::to_array(out);
        if (auto decoded = decode_root(bytes, temporary, context, options); !decoded) {
            return decoded;
        }
        for (std::size_t index = 0; index != Size; ++index) {
            using std::swap;
            swap(out[index], temporary[index]);
        }
        return {};
    }

    template<typename T, std::same_as<serialization_context> Context>
        requires serializable<T> && std::default_initializable<codec_value_type<T>>
    [[nodiscard]] std::expected<codec_value_type<T>, error> extract(
        std::span<const std::byte> bytes,
        const Context &context,
        decode_options options = {}) {
        codec_value_type<T> result{};
        if (auto decoded = decode_root(bytes, result, context, options); !decoded) {
            return std::unexpected{decoded.error()};
        }
        return result;
    }

    template<context_free_serializable T>
    [[nodiscard]] std::expected<void, error> decode_root(
        std::span<const std::byte> bytes, T &out, decode_options options = {}) {
        return decode_root(bytes, out, serialization_context{}, options);
    }

    template<context_free_serializable T>
        requires std::copy_constructible<codec_value_type<T>>
                 && std::is_nothrow_swappable_v<codec_value_type<T>>
    [[nodiscard]] std::expected<void, error> unpack(
        std::span<const std::byte> bytes, T &out, decode_options options = {}) {
        return unpack(bytes, out, serialization_context{}, options);
    }

    template<typename T, std::size_t Size>
        requires context_free_serializable<T[Size]>
                 && std::copy_constructible<T> && std::is_nothrow_swappable_v<T>
    [[nodiscard]] std::expected<void, error> unpack(
        std::span<const std::byte> bytes, T (&out)[Size], decode_options options = {}) {
        return unpack(bytes, out, serialization_context{}, options);
    }

    template<context_free_serializable T>
        requires std::default_initializable<codec_value_type<T>>
    [[nodiscard]] std::expected<codec_value_type<T>, error> extract(
        std::span<const std::byte> bytes, decode_options options = {}) {
        return extract<T>(bytes, serialization_context{}, options);
    }
}

