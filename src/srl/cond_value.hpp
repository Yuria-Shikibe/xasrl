#pragma once
#include <concepts>
#include <functional>
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace xasrl::detail {
    template <typename T, bool Exist, T = {}>
    struct cond_value {
        static constexpr bool exist = Exist;
        T value{};

        constexpr cond_value() = default;
        constexpr cond_value(const cond_value &) = default;
        constexpr cond_value(cond_value &&) = default;
        constexpr cond_value &operator=(const cond_value &) = default;
        constexpr cond_value &operator=(cond_value &&) = default;

        template <typename... Args>
            requires (sizeof...(Args) > 0 &&
                      !(sizeof...(Args) == 1 && (std::same_as<std::remove_cvref_t<Args>, cond_value> && ...)) &&
                      std::is_constructible_v<T, Args...>)
        constexpr explicit(sizeof...(Args) != 1 || !(std::is_convertible_v<Args, T> && ...))
        cond_value(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
            : value(std::forward<Args>(args)...) {}

        template <typename U>
            requires std::is_constructible_v<T, std::initializer_list<U>>
        constexpr explicit(!std::is_convertible_v<std::initializer_list<U>, T>)
        cond_value(std::initializer_list<U> values)
            noexcept(std::is_nothrow_constructible_v<T, std::initializer_list<U>>)
            : value(std::move(values)) {}

        template <typename U>
            requires std::is_assignable_v<T &, std::initializer_list<U>>
        constexpr cond_value &operator=(std::initializer_list<U> values)
            noexcept(std::is_nothrow_assignable_v<T &, std::initializer_list<U>>) {
            value = std::move(values);
            return *this;
        }

        template <typename U>
            requires (!std::same_as<std::remove_cvref_t<U>, cond_value> && std::is_assignable_v<T &, U>)
        constexpr cond_value &operator=(U &&other) noexcept(std::is_nothrow_assignable_v<T &, U>) {
            value = std::forward<U>(other);
            return *this;
        }

        constexpr operator T &() & noexcept { return value; }
        constexpr operator const T &() const & noexcept { return value; }
        constexpr operator T &&() && noexcept { return std::move(value); }
        constexpr operator const T &&() const && noexcept { return std::move(value); }

        template <typename Self, typename... Args>
            requires std::invocable<decltype((std::declval<Self>().value)), Args...>
        constexpr decltype(auto) operator()(this Self &&self, Args &&...args)
            noexcept(std::is_nothrow_invocable_v<decltype((std::forward<Self>(self).value)), Args...>) {
            return std::invoke(std::forward<Self>(self).value, std::forward<Args>(args)...);
        }
    };

    template <typename T, T Val>
    struct cond_value<T, false, Val> {
        static constexpr bool exist = false;
        static constexpr T value = Val;

        constexpr cond_value() = default;
        constexpr cond_value(const cond_value &) = default;
        constexpr cond_value(cond_value &&) = default;
        constexpr cond_value &operator=(const cond_value &) = default;
        constexpr cond_value &operator=(cond_value &&) = default;

        // Absent storage accepts the same inputs without constructing or mutating T.
        template <typename... Args>
            requires (sizeof...(Args) > 0 &&
                      !(sizeof...(Args) == 1 && (std::same_as<std::remove_cvref_t<Args>, cond_value> && ...)) &&
                      std::is_constructible_v<T, Args...>)
        constexpr explicit(sizeof...(Args) != 1 || !(std::is_convertible_v<Args, T> && ...))
        cond_value(Args &&...) noexcept {}

        template <typename U>
            requires std::is_constructible_v<T, std::initializer_list<U>>
        constexpr explicit(!std::is_convertible_v<std::initializer_list<U>, T>)
        cond_value(std::initializer_list<U>) noexcept {}

        template <typename U>
            requires std::is_assignable_v<T &, std::initializer_list<U>>
        constexpr cond_value &operator=(std::initializer_list<U>) noexcept {
            return *this;
        }

        template <typename U>
            requires (!std::same_as<std::remove_cvref_t<U>, cond_value> && std::is_assignable_v<T &, U>)
        constexpr cond_value &operator=(U &&) noexcept {
            return *this;
        }

        constexpr operator const T &() const noexcept { return value; }

        template <typename... Args>
            requires std::invocable<const T &, Args...>
        constexpr decltype(auto) operator()(Args &&...args) const
            noexcept(std::is_nothrow_invocable_v<const T &, Args...>) {
            return std::invoke(value, std::forward<Args>(args)...);
        }
    };

}
