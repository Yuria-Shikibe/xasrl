#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace srl {
    enum class duplicate_policy : std::uint8_t {
        deduplicate,
        reject,
    };

    struct decode_options {
        duplicate_policy duplicates{duplicate_policy::deduplicate};
    };

    namespace detail {
        template<typename T, typename = void>
        struct default_container_traits_impl {
            using element_type = std::ranges::range_value_t<T>;
            static constexpr std::uint8_t mapping_group_size = 1;
            static constexpr std::size_t fixed_size = std::dynamic_extent;
        };

        template<typename T>
        struct default_container_traits_impl<
            T,
            std::void_t<typename T::key_type, typename T::mapped_type>> {
            using key_type = typename T::key_type;
            using mapped_type = typename T::mapped_type;
            using element_type = std::pair<key_type, mapped_type>;
            static constexpr std::uint8_t mapping_group_size = 2;
            static constexpr std::size_t fixed_size = std::dynamic_extent;
        };
    }

    template<typename T>
    struct default_container_traits : detail::default_container_traits_impl<T> {};

    template<typename T, typename = void>
    struct container_traits {};

    template<typename T>
        requires std::ranges::forward_range<const T>
    struct container_traits<T, void> : default_container_traits<T> {};

    template<typename T, std::size_t Size>
    struct container_traits<std::array<T, Size>, void> {
        using element_type = T;
        static constexpr std::uint8_t mapping_group_size = 1;
        static constexpr std::size_t fixed_size = Size;
    };

    template<typename T, std::size_t Size>
    struct container_traits<T[Size], void> {
        using element_type = T;
        static constexpr std::uint8_t mapping_group_size = 1;
        static constexpr std::size_t fixed_size = Size;
    };

    namespace detail {
        template<typename T>
        concept has_container_traits = requires {
            typename container_traits<T>::element_type;
            { container_traits<T>::mapping_group_size } -> std::convertible_to<std::uint8_t>;
            { container_traits<T>::fixed_size } -> std::convertible_to<std::size_t>;
        };

        template<typename T>
        [[nodiscard]] constexpr decltype(auto) container_range(T &value) {
            if constexpr (requires { container_traits<T>::range(value); }) {
                return container_traits<T>::range(value);
            } else {
                return (value);
            }
        }

        template<typename T>
        [[nodiscard]] constexpr decltype(auto) container_range(const T &value) {
            if constexpr (requires { container_traits<T>::range(value); }) {
                return container_traits<T>::range(value);
            } else {
                return (value);
            }
        }

        template<typename T>
        concept fixed_container = has_container_traits<T>
                                  && container_traits<T>::fixed_size != std::dynamic_extent;

        template<typename T>
        concept clearable_container = requires(T &value) {
            value.clear();
        } || requires(T &value) {
            container_traits<T>::clear(value);
        };

        template<typename T>
        concept insert_after_container = requires(
            T &value,
            typename container_traits<T>::element_type element) {
            value.before_begin();
            value.insert_after(value.before_begin(), std::move(element));
        };

        template<typename T>
        concept ordinary_insertable_container = requires(
            T &value,
            typename container_traits<T>::element_type element) {
            container_traits<T>::insert(value, std::move(element));
        } || requires(T &value, typename container_traits<T>::element_type element) {
            value.emplace_back(std::move(element));
        } || requires(T &value, typename container_traits<T>::element_type element) {
            value.push_back(std::move(element));
        } || requires(T &value, typename container_traits<T>::element_type element) {
            value.insert(value.end(), std::move(element));
        } || requires(T &value, typename container_traits<T>::element_type element) {
            value.emplace(std::move(element));
        } || requires(T &value, typename container_traits<T>::element_type element) {
            value.insert(std::move(element));
        };

        template<typename T>
        concept adapted_container = has_container_traits<T>
                                    && std::ranges::forward_range<
                                        decltype(container_range(std::declval<const T &>()))>
                                    && (container_traits<T>::mapping_group_size == 1
                                        || container_traits<T>::mapping_group_size == 2)
                                    && (fixed_container<T>
                                        || (clearable_container<T>
                                            && (insert_after_container<T>
                                                || ordinary_insertable_container<T>)))
                                    && !std::same_as<std::remove_cv_t<T>, std::string>;

        template<typename T>
        void clear_container(T &value) {
            if constexpr (requires { container_traits<T>::clear(value); }) {
                container_traits<T>::clear(value);
            } else {
                value.clear();
            }
        }

        template<typename T>
        void reserve_container(T &value, std::size_t size) {
            if constexpr (requires { container_traits<T>::reserve(value, size); }) {
                container_traits<T>::reserve(value, size);
            } else if constexpr (requires { value.reserve(size); }) {
                value.reserve(size);
            }
        }

        template<typename Result>
        [[nodiscard]] constexpr bool insertion_succeeded(Result &&result) {
            if constexpr (requires { static_cast<bool>(result.second); }) {
                return static_cast<bool>(result.second);
            } else if constexpr (std::convertible_to<Result, bool>) {
                return static_cast<bool>(result);
            } else {
                return true;
            }
        }

        template<typename T>
        [[nodiscard]] bool insert_container_value(
            T &value,
            typename container_traits<T>::element_type element) {
            if constexpr (requires { container_traits<T>::insert(value, std::move(element)); }) {
                if constexpr (std::is_void_v<decltype(
                                  container_traits<T>::insert(value, std::move(element)))>) {
                    container_traits<T>::insert(value, std::move(element));
                    return true;
                } else {
                    return detail::insertion_succeeded(
                        container_traits<T>::insert(value, std::move(element)));
                }
            } else if constexpr (requires { value.emplace_back(std::move(element)); }) {
                value.emplace_back(std::move(element));
                return true;
            } else if constexpr (requires { value.push_back(std::move(element)); }) {
                value.push_back(std::move(element));
                return true;
            } else if constexpr (requires { value.emplace(std::move(element)); }) {
                return detail::insertion_succeeded(value.emplace(std::move(element)));
            } else if constexpr (requires { value.insert(std::move(element)); }) {
                return detail::insertion_succeeded(value.insert(std::move(element)));
            } else if constexpr (requires { value.insert(value.end(), std::move(element)); }) {
                value.insert(value.end(), std::move(element));
                return true;
            } else {
                static_assert(sizeof(T) == 0, "container has no supported insertion operation");
            }
        }
    }
}
