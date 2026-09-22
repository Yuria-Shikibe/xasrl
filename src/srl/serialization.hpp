#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <expected>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "detail/decode.hpp"
#include "detail/encode.hpp"

namespace srl {
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

    template<serializable T>
    [[nodiscard]] std::expected<void, error> decode_root(
        std::span<const std::byte> bytes,
        T &out,
        const serialization_context &context) {
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
            return detail::decode_value(entry->content, out, context, 0, 0);
        } else if constexpr (container_serializable<T>) {
            return detail::decode_container(*entry, out, context, std::nullopt);
        } else {
            return detail::decode_record(*entry, out, context);
        }
    }

    template<serializable T>
        requires std::copy_constructible<codec_value_type<T>>
                 && std::is_nothrow_swappable_v<codec_value_type<T>>
    [[nodiscard]] std::expected<void, error> unpack(
        std::span<const std::byte> bytes,
        T &out,
        const serialization_context &context) {
        codec_value_type<T> temporary = out;
        if (auto decoded = srl::decode_root(bytes, temporary, context); !decoded) {
            return decoded;
        }
        using std::swap;
        swap(out, temporary);
        return {};
    }

    template<typename T, std::size_t Size>
        requires serializable<T[Size]>
                 && std::copy_constructible<T>
                 && std::is_nothrow_swappable_v<T>
    [[nodiscard]] std::expected<void, error> unpack(
        std::span<const std::byte> bytes,
        T (&out)[Size],
        const serialization_context &context) {
        auto temporary = std::to_array(out);
        if (auto decoded = srl::decode_root(bytes, temporary, context); !decoded) {
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
        if (auto decoded = decode_root(bytes, result, context); !decoded) {
            return std::unexpected{decoded.error()};
        }
        return result;
    }
}
