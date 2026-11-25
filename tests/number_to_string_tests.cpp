#include "srl/number_to_string.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace {
    static_assert(std::is_same_v<xasrl::smallest_uint_t<0>, std::uint8_t>);
    static_assert(std::is_same_v<xasrl::smallest_uint_t<255>, std::uint8_t>);
    static_assert(std::is_same_v<xasrl::smallest_uint_t<256>, std::uint16_t>);
    static_assert(std::is_same_v<xasrl::smallest_uint_t<65535>, std::uint16_t>);
    static_assert(std::is_same_v<xasrl::smallest_uint_t<65536>, std::uint32_t>);
    static_assert(std::is_same_v<xasrl::smallest_uint_t<0xffffffff>, std::uint32_t>);
    static_assert(std::is_same_v<xasrl::smallest_uint_t<0x100000000>, std::uint64_t>);

    constexpr auto zero = xasrl::number_to_string(std::uint8_t{0});
    static_assert(zero.view() == "0");
    static_assert(std::is_same_v<decltype(zero.buffer()), const std::array<char, 3> &>);

    constexpr auto min_binary = xasrl::number_to_string<2>(std::numeric_limits<std::int8_t>::min());
    static_assert(min_binary.view() == "-10000000" && min_binary.buffer().size() == 9);

    constexpr auto max_hex = xasrl::number_to_string<16>(std::numeric_limits<std::uint64_t>::max());
    static_assert(max_hex.view() == "ffffffffffffffff" && max_hex.buffer().size() == 16);

    constexpr auto min_decimal = xasrl::number_to_string(std::numeric_limits<std::int64_t>::min());
    static_assert(min_decimal.view() == "-9223372036854775808" && min_decimal.buffer().size() == 20);

    constexpr auto ternary = xasrl::number_to_string<3>(std::uint8_t{255});
    static_assert(ternary.view() == "100110" && ternary.buffer().size() == 6);

    constexpr auto wide = xasrl::number_to_string<36, wchar_t>(-1295);
    static_assert(wide.view() == L"-zz");

    template<unsigned Base>
    concept valid_base = requires { xasrl::number_to_string<Base>(0); };
    static_assert(!valid_base<1> && !valid_base<37>);

    template<typename T>
    concept view_from_temporary = requires(T value) { std::move(value).view(); };
    static_assert(!view_from_temporary<decltype(zero)>);

    TEST(NumberToString, CopiesAndMovesViewTheirOwnStorage) {
        const auto value = xasrl::number_to_string<16>(-42);
        auto copy = value;
        const auto moved = std::move(copy);
        EXPECT_EQ(moved.view(), "-2a");
        EXPECT_EQ(moved.view().data(), moved.buffer().data());
    }
}
