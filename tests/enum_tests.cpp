#include "srl/enum.hpp"

#include <gtest/gtest.h>

namespace {
    enum class empty : int {};
    enum class boolean : bool { yes = true };
    enum class runs : std::int8_t {
        last = 127, a = -128, b = -127, c = -126, alias = -127,
        d = -2, e = -1, f = 0, g = 1, h = 2, i = 125, j = 126,
    };
    enum class bitmap : std::uint8_t { a = 0, b = 2, c = 63, d = 64, e = 66, f = 127, g = 255 };
    enum class extremes : std::int64_t {
        a = INT64_MIN, b = INT64_MIN + 1, c = -1, d = 0, e = 1,
        f = INT64_MAX - 1, g = INT64_MAX,
    };
    enum class wide : std::uint64_t { a = 0, b = 63, c = UINT64_MAX - 1, d = UINT64_MAX };
    enum class [[=srl::tag::bit_flag{0xff}]] all_flags : std::uint8_t { a = 1 };
    enum class [[=srl::tag::bit_flag{0x83}]] flags : std::uint8_t { a = 1, b = 2, c = 128 };
    enum class [[=srl::tag::bit_flag{0}]] inferred_flags : std::uint8_t { none = 0, ab = 3, alias = 3, c = 128 };
    enum class [[=srl::tag::bit_flag{}]] bool_flags : bool { yes = true };
    enum class [[=srl::tag::bit_flag{0}]] inferred_high_flags : std::uint64_t { high = 1ull << 63, low = 3 };
    enum class [[=srl::tag::bit_flag{UINT64_MAX}]] full_flags : std::uint64_t {};
    enum class [[=srl::tag::bit_flag{0x8000000000000003ull}]] high_flags : std::uint64_t {};

    template<typename E>
    constexpr bool reference(std::underlying_type_t<E> value) {
        template for (constexpr auto e: std::define_static_array(std::meta::enumerators_of(^^E))) {
            if (value == std::to_underlying(std::meta::extract<E>(std::meta::constant_of(e)))) { return true; }
        }
        return false;
    }

    static_assert(!srl::enum_contains<empty>(0));
    static_assert(srl::enum_contains(boolean::yes));
    static_assert(!srl::enum_contains<boolean>(false));
    static_assert(srl::enum_contains<bool_flags>(true));
    static_assert(srl::enum_contains<bool_flags>(false));
    static_assert(srl::enum_contains<runs>(-128));
    static_assert(!srl::enum_contains<runs>(-125));
    static_assert(srl::enum_contains<bitmap>(63));
    static_assert(!srl::enum_contains<bitmap>(62));
    static_assert(srl::enum_contains<extremes>(INT64_MIN));
    static_assert(srl::enum_contains<extremes>(INT64_MAX));
    static_assert(!srl::enum_contains<extremes>(INT64_MIN + 2));
    static_assert(srl::enum_contains<wide>(UINT64_MAX));
    static_assert(!srl::enum_contains<wide>(UINT64_MAX - 2));
    static_assert(srl::enum_contains<all_flags>(255));
    static_assert(srl::enum_contains<full_flags>(UINT64_MAX));
    static_assert(srl::enum_contains<high_flags>(0x8000000000000003ull));
    static_assert(!srl::enum_contains<high_flags>(4));
    static_assert(srl::enum_contains<inferred_high_flags>(0x8000000000000003ull));
    static_assert(!srl::enum_contains<inferred_high_flags>(4));

    // The builder returns values: callers can mutate them during constant
    // evaluation and use individual partitions as structural template arguments.
    template<srl::detail::enum_partition Partition>
    constexpr bool partition_constant = Partition.first <= Partition.last;
    constexpr auto first_partition = [] consteval {
        auto partitions = srl::detail::make_enum_partitions(^^bitmap);
        return partitions.front();
    }();
    static_assert(partition_constant<first_partition>);
    static_assert([] consteval {
        auto partitions = srl::detail::make_enum_partitions(^^bitmap);
        partitions.front().bits = 0;
        return partitions.front().bits == 0 && !partitions.empty();
    }());
    static_assert(srl::detail::make_enum_partitions(^^empty).empty());
    static_assert([] consteval {
        for (unsigned width: {0u, 1u, 16u, 32u, 64u}) {
            const auto partitions = srl::detail::make_enum_partitions(^^runs, width);
            for (int value = -128; value <= 127; ++value) {
                const auto key = srl::detail::enum_order_key(static_cast<std::uint64_t>(value), true);
                bool found = false;
                for (const auto partition: partitions) { found |= partition.contains(key); }
                if (found != reference<runs>(value)) { return false; }
            }
        }
        return true;
    }());

    enum class extended : unsigned __int128 {
        zero = 0, high = static_cast<unsigned __int128>(1) << 100,
        next = (static_cast<unsigned __int128>(1) << 100) + 1,
        maximum = ~static_cast<unsigned __int128>(0),
    };
    enum class extended_signed : __int128 {
        low = -(static_cast<__int128>(1) << 100),
        minus_one = -1, zero = 0,
        high = static_cast<__int128>(1) << 100,
    };
    static_assert(srl::enum_contains(extended::high));
    static_assert(srl::enum_contains(extended::maximum));
    static_assert(!srl::enum_contains<extended>(std::to_underlying(extended::high) + 2));
    static_assert(!srl::enum_contains<extended>(1));
    static_assert(srl::enum_contains(extended_signed::low));
    static_assert(srl::enum_contains(extended_signed::high));
    static_assert(srl::enum_contains(extended_signed::minus_one));
    static_assert(!srl::enum_contains<extended_signed>(std::to_underlying(extended_signed::low) + 1));

    TEST(EnumContains, ExhaustiveSmallDomains) {
        for (int value = -128; value <= 127; ++value) {
            EXPECT_EQ(srl::enum_contains<runs>(value), reference<runs>(value)) << value;
        }
        for (unsigned value = 0; value <= 255; ++value) {
            EXPECT_EQ(srl::enum_contains<bitmap>(value), reference<bitmap>(value)) << value;
            EXPECT_EQ(srl::enum_contains<flags>(value), (value & ~0x83u) == 0) << value;
            EXPECT_TRUE(srl::enum_contains<all_flags>(value));
            EXPECT_EQ(srl::enum_contains<inferred_flags>(value), srl::enum_contains<flags>(value));
        }
    }

    TEST(EnumContains, WideBoundariesAndHoles) {
        for (const auto value: {INT64_MIN, INT64_MIN + 1, INT64_MIN + 2, std::int64_t{-2},
                 std::int64_t{-1}, std::int64_t{0}, std::int64_t{1}, std::int64_t{2},
                 INT64_MAX - 2, INT64_MAX - 1, INT64_MAX}) {
            EXPECT_EQ(srl::enum_contains<extremes>(value), reference<extremes>(value)) << value;
        }
        for (const auto value: {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{62},
                 std::uint64_t{63}, std::uint64_t{64}, UINT64_MAX - 2, UINT64_MAX - 1, UINT64_MAX}) {
            EXPECT_EQ(srl::enum_contains<wide>(value), reference<wide>(value)) << value;
        }
    }
}
