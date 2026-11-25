#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <meta>
#include <limits>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "annotations.hpp"
#include "schema/diagnostic.hpp"

namespace srl {
    namespace detail {
#if defined(__SIZEOF_INT128__)
        using enum_max_underlying_type = unsigned __int128;
#else
        // Standard enum underlying types are no wider than uintmax_t.  Keep
        // the implementation usable on compilers without the __int128 extension.
        using enum_max_underlying_type = std::uintmax_t;
#endif

        inline constexpr unsigned enum_max_underlying_bits = std::numeric_limits<enum_max_underlying_type>::digits;
        inline constexpr unsigned enum_u64_bits = std::numeric_limits<std::uint64_t>::digits;
        inline constexpr unsigned enum_byte_bits = std::numeric_limits<unsigned char>::digits;

        struct enum_policy {
            bool flags = false;
            std::uint64_t mask{};
        };

        // Extraction requires the exact enum type. One function specialization per
        // enum converts all of its values; enumerators are never template arguments.
        template<typename Enum>
        consteval enum_max_underlying_type extract_enum_number(std::meta::info value) {
            return static_cast<enum_max_underlying_type>(std::meta::extract<Enum>(value));
        }

        consteval auto enum_number_extractor(std::meta::info type) {
            return std::meta::extract<enum_max_underlying_type (*)(std::meta::info)>(
                std::meta::substitute(^^extract_enum_number, {type}));
        }

        // Bias signed values into unsigned order without overflowing at INT64_MIN.
        constexpr std::uint64_t enum_order_key(std::uint64_t value, bool is_signed) noexcept {
            return value ^ (is_signed ? std::uint64_t{1} << (enum_u64_bits - 1) : 0);
        }

        constexpr enum_max_underlying_type enum_order_key_max(
            enum_max_underlying_type value, bool is_signed) noexcept {
            return value ^ (is_signed
                ? static_cast<enum_max_underlying_type>(1) << (enum_max_underlying_bits - 1)
                : 0);
        }

        struct enum_partition {
            enum_max_underlying_type first;
            enum_max_underlying_type last;
            std::uint64_t bits;
            bool contiguous;

            constexpr bool contains(std::uint64_t key) const noexcept {
                // The 64-bit consumer only receives partitions built for <=64-bit enums.
                const auto offset = key - static_cast<std::uint64_t>(first);
                return offset <= static_cast<std::uint64_t>(last - first)
                    && (contiguous || ((bits >> offset) & 1) != 0);
            }

            constexpr bool contains_max(enum_max_underlying_type key) const noexcept {
                const auto offset = key - first;
                return offset <= last - first && (contiguous || ((bits >> offset) & 1) != 0);
            }
        };

        consteval auto make_enum_partitions(
            std::meta::info type, unsigned bitmap_width = enum_u64_bits) {
            if (bitmap_width > enum_u64_bits) {
                reject_schema(schema_error::unsupported_type, {}, type, "enum bitmap width exceeds 64");
            }
            const auto extract = enum_number_extractor(type);
            const bool is_signed = std::meta::is_signed_type(std::meta::underlying_type(type));
            const bool wide = std::meta::size_of(type) > sizeof(std::uint64_t);
            std::vector<enum_max_underlying_type> values;
            for (auto enumerator: std::meta::enumerators_of(type)) {
                const auto value = extract(std::meta::constant_of(enumerator));
                values.push_back(wide ? enum_order_key_max(value, is_signed)
                    : enum_order_key(static_cast<std::uint64_t>(value), is_signed));
            }
            std::ranges::sort(values);
            values.erase(std::ranges::unique(values).begin(), values.end());
            std::vector<enum_partition> partitions;
            for (std::size_t begin = 0; begin < values.size();) {
                auto end = begin + 1;
                while (end < values.size() && values[end] - values[end - 1] == 1) { ++end; }
                std::uint64_t bits = 0;
                if (end - begin < bitmap_width) {
                    end = begin;
                    while (end < values.size()) {
                        const auto offset = values[end] - values[begin];
                        if (offset >= bitmap_width) { break; }
                        bits |= std::uint64_t{1} << offset;
                        ++end;
                    }
                }
                partitions.push_back({values[begin], values[end - 1], bits,
                                      values[end - 1] - values[begin] == end - begin - 1});
                begin = end;
            }
            return partitions;
        }

        consteval enum_policy make_enum_policy(std::meta::info e) {
            const auto annotations = std::meta::annotations_of_with_type(e, ^^tag::bit_flag);
            if (annotations.empty()) { return enum_policy{}; }
            if (annotations.size() != 1) { reject_schema(schema_error::conflicting_annotations, {}, e, "flags requires exactly one annotation"); }

            const auto underlying = std::meta::underlying_type(e);
            if ((!std::meta::is_unsigned_type(underlying) && underlying != ^^bool)
                || std::meta::size_of(underlying) > sizeof(std::uint64_t)) {
                reject_schema(schema_error::conflicting_annotations, {}, e,
                    "flags requires an unsigned underlying type of at most 64 bits");
            } else {
                auto mask = std::meta::extract<tag::bit_flag>(annotations.front()).mask;
                const auto bits = std::meta::size_of(underlying) * enum_byte_bits;
                const auto maximum = underlying == ^^bool ? 1
                    : std::numeric_limits<std::uint64_t>::max() >> (enum_u64_bits - bits);
                if (mask > maximum) {
                    reject_schema(schema_error::conflicting_annotations, {}, e, "flags mask exceeds the underlying type range");
                }
                const auto extract = enum_number_extractor(e);
                std::uint64_t enumerator_mask{};
                for (auto enumerator: std::meta::enumerators_of(e)) {
                    const auto value = extract(std::meta::constant_of(enumerator));
                    enumerator_mask |= static_cast<std::uint64_t>(value);
                }
                if (mask == 0) { mask = enumerator_mask; }
                if (mask == 0) { reject_schema(schema_error::conflicting_annotations, {}, e, "flags mask must be nonzero"); }
                if ((enumerator_mask & ~mask) != 0) {
                    reject_schema(schema_error::conflicting_annotations, {}, e,
                        std::string("flags mask=") + number_to_string(mask)
                        + " does not cover enumerator bits=" + number_to_string(enumerator_mask));
                }
                return enum_policy{.flags = true, .mask = mask};
            }
        }

        consteval schema_issue enum_metadata_issue(std::meta::info type) {
            if (!std::meta::is_enum_type(type)) {
                if (!std::meta::annotations_of_with_type(type, ^^tag::bit_flag).empty()) {
                    return {schema_error::conflicting_annotations, type, {}, "flags requires an enum type"};
                }
                return {};
            }
            try {
                (void)make_enum_policy(type);
                return {};
            } catch (const schema_failure &failure) {
                return failure.issue();
            }
        }

        consteval bool enum_metadata_valid(std::meta::info type) {
            return enum_metadata_issue(type).reason == schema_error::none;
        }
    }

    template<typename Enum>
        requires (std::meta::is_enum_type(^^Enum))
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
        requires (std::meta::is_enum_type(^^Enum))
    [[nodiscard]] constexpr bool enum_contains(typename[:std::meta::underlying_type(^^Enum):] value) noexcept {
        if constexpr (detail::make_enum_policy(^^Enum).flags) {
            return (static_cast<std::uint64_t>(value) & ~detail::make_enum_policy(^^Enum).mask) == 0;
        } else {
            static constexpr auto partitions = [] consteval {
                constexpr auto partitions = std::define_static_array(detail::make_enum_partitions(^^Enum));
                std::array<detail::enum_partition, partitions.size()> result{};
                std::ranges::copy(partitions, result.begin());
                return result;
            }();

            constexpr bool is_signed = std::meta::is_signed_type(std::meta::underlying_type(^^Enum));
            const auto key = [&] {
                if constexpr (sizeof(Enum) > sizeof(std::uint64_t)) {
                    return detail::enum_order_key_max(
                        static_cast<detail::enum_max_underlying_type>(value), is_signed);
                } else {
                    return detail::enum_order_key(static_cast<std::uint64_t>(value), is_signed);
                }
            }();

            template for (constexpr detail::enum_partition partition: partitions) {
                if constexpr (sizeof(Enum) > sizeof(std::uint64_t)) {
                    if (partition.contains_max(key)) { return true; }
                } else {
                    if (partition.contains(key)) { return true; }
                }
            }

            return false;
        }
    }

    template<typename Enum>
        requires (std::meta::is_enum_type(^^Enum))
    [[nodiscard]] constexpr bool enum_contains(Enum value) noexcept {
        return srl::enum_contains<Enum>(std::to_underlying(value));
    }

}
