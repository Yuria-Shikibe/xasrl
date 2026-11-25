#include "srl/cond_value.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
    using xasrl::detail::cond_value;
    using stored = cond_value<int, true>;
    using omitted = cond_value<int, false, 42>;

    struct callable {
        int value{};
        constexpr int &operator()() & noexcept { return value; }
        constexpr const int &operator()() const & noexcept { return value; }
        constexpr int operator()() && noexcept { return value + 10; }
        constexpr int operator()() const && noexcept { return value + 20; }
    };

    struct move_only {
        int value{};
        constexpr move_only() = default;
        constexpr explicit move_only(int v) noexcept : value(v) {}
        move_only(const move_only &) = delete;
        constexpr move_only(move_only &&other) noexcept : value(std::exchange(other.value, -1)) {}
        constexpr move_only &operator=(move_only &&other) noexcept {
            value = std::exchange(other.value, -1);
            return *this;
        }
    };

    struct pair_value {
        int first{};
        int second{};
        constexpr pair_value() = default;
        constexpr pair_value(int a, int b) noexcept : first(a), second(b) {}
    };

    struct list_value {
        int sum{};
        constexpr list_value() = default;
        constexpr list_value(std::initializer_list<int> values) {
            for (int value : values) sum += value;
        }
    };

    struct throwing_value {
        int value{};
        constexpr throwing_value() = default;
        explicit throwing_value(int) { throw std::runtime_error("construction"); }
        throwing_value &operator=(int) { throw std::runtime_error("assignment"); }
        void operator()() const { throw std::runtime_error("invocation"); }
    };

    static_assert(std::is_empty_v<omitted>);
    static_assert(sizeof(stored) == sizeof(int));
    static_assert(std::is_trivially_copyable_v<stored>);
    static_assert(std::is_convertible_v<int, stored>);
    static_assert(std::is_convertible_v<stored &, int &>);
    static_assert(std::is_convertible_v<const stored &, const int &>);
    static_assert(std::is_convertible_v<stored &&, int &&>);
    static_assert(std::is_convertible_v<const stored &&, const int &&>);
    static_assert(!std::is_convertible_v<omitted &, int &>);
    static_assert(!std::is_constructible_v<stored, const char *>);
    static_assert(!std::is_assignable_v<stored &, const char *>);
    static_assert(!std::is_invocable_v<stored>);
    static_assert(!std::is_copy_constructible_v<cond_value<move_only, true>>);
    static_assert(!std::is_convertible_v<int, cond_value<move_only, true>>);
    static_assert(std::is_nothrow_constructible_v<cond_value<move_only, true>, int>);
    static_assert(!std::is_nothrow_constructible_v<cond_value<throwing_value, true>, int>);
    static_assert(std::is_nothrow_constructible_v<cond_value<throwing_value, false>, int>);
    static_assert(!std::is_nothrow_assignable_v<cond_value<throwing_value, true> &, int>);
    static_assert(std::is_nothrow_assignable_v<cond_value<throwing_value, false> &, int>);
    static_assert(!std::is_nothrow_invocable_v<cond_value<throwing_value, false>>);
    static_assert([] {
        stored a = 3;
        a = 7;
        int &reference = a;
        ++reference;
        omitted b = 99;
        b = a;
        return a == 8 && b == 42 && stored{} == 0;
    }());
    static_assert(cond_value<pair_value, true>(3, 4).value.second == 4);
    static_assert(cond_value<list_value, true>{1, 2, 3}.value.sum == 6);
    static_assert(cond_value<list_value, false>{1, 2, 3}.value.sum == 0);
    static_assert([] {
        cond_value<list_value, true> a = {1, 2};
        a = {3, 4};
        cond_value<list_value, false> b = {1, 2};
        b = {3, 4};
        return a.value.sum == 7 && b.value.sum == 0;
    }());

    TEST(CondValue, ConvertsAssignsAndCopies) {
        stored a = 3;
        stored b = a;
        b = 7;
        int &reference = b;
        ++reference;
        EXPECT_EQ(a.value, 3);
        EXPECT_EQ(b.value, 8);
        a = b;
        EXPECT_EQ(a.value, 8);
        omitted absent = a;
        absent = 100;
        a = absent;
        EXPECT_EQ(a.value, 42);
        EXPECT_EQ(static_cast<int>(absent), 42);
    }

    TEST(CondValue, MovesUnderlyingValue) {
        cond_value<move_only, true> a(7);
        auto b = std::move(a);
        EXPECT_EQ(a.value.value, -1);
        EXPECT_EQ(b.value.value, 7);
        b = move_only(9);
        move_only result = std::move(b);
        EXPECT_EQ(result.value, 9);
        EXPECT_EQ(b.value.value, -1);
    }

    TEST(CondValue, PreservesCallQualifiersAndReferences) {
        cond_value<callable, true> a(callable{5});
        a() = 6;
        EXPECT_EQ(std::as_const(a)(), 6);
        EXPECT_EQ(std::move(a)(), 16);
        EXPECT_EQ(std::move(std::as_const(a))(), 26);
        static_assert(noexcept(a()));
        cond_value<callable, false, callable{42}> absent(callable{99});
        static_assert(std::same_as<decltype(absent()), const int &>);
        EXPECT_EQ(absent(), 42);
        EXPECT_EQ(std::move(absent)(), 42);
    }

    TEST(CondValue, InvokesMemberPointersAndForwardsArguments) {
        cond_value<int callable::*, true> member(&callable::value);
        callable object{3};
        member(object) = 9;
        EXPECT_EQ(object.value, 9);
        constexpr auto consume = [](move_only value) { return value.value; };
        cond_value<decltype(consume), true> function;
        EXPECT_EQ(function(move_only{12}), 12);
    }

    TEST(CondValue, PropagatesFailuresOnlyForExecutedOperations) {
        using present = cond_value<throwing_value, true>;
        EXPECT_THROW((present{1}), std::runtime_error);
        present a;
        EXPECT_THROW(a = 1, std::runtime_error);
        EXPECT_THROW(a(), std::runtime_error);
        cond_value<throwing_value, false> absent(1);
        EXPECT_NO_THROW(absent = 2);
        EXPECT_THROW(absent(), std::runtime_error);
    }
}
