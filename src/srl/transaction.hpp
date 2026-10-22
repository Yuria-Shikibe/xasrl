#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <meta>
#include <ranges>
#include <utility>
#include <vector>

#include "detail/reflection.hpp"

namespace srl {
    // A customization owns both construction and commit: clone must preserve
    // every precondition of commit, including allocator equality.
    template<typename T>
    struct transaction_traits;

    namespace detail {
        template<typename T>
        concept has_adl_swap = requires(T &left, T &right) { swap(left, right); };

        // These probes preserve C++ lookup and user customization semantics.
        template<typename T>
        concept has_transaction_commit = requires(T &left, T &right) {
            { transaction_traits<T>::commit(left, right) } noexcept;
        };

        consteval bool transaction_commit_supported(std::meta::info type) {
            if (std::meta::is_nothrow_swappable_type(type)) { return true; }
            if (!std::meta::is_aggregate_type(type) || std::meta::is_array_type(type)
                || std::meta::extract<bool>(std::meta::substitute(^^has_adl_swap, {type}))) {
                return false;
            }
            for (auto entity: reflected_entities(type)) {
                const auto member = std::meta::remove_cvref(std::meta::type_of(entity));
                if (!std::meta::extract<bool>(std::meta::substitute(^^has_transaction_commit, {member}))) {
                    return false;
                }
            }
            return true;
        }

        enum class allocator_domain { absent, preserved, changed };

        template<typename T>
        inline constexpr allocator_domain allocator_copy_domain = [] consteval {
            if constexpr (requires { typename T::allocator_type; }) {
                using traits = std::allocator_traits<typename T::allocator_type>;
                return traits::is_always_equal::value || traits::propagate_on_container_swap::value
                    ? allocator_domain::preserved : allocator_domain::changed;
            } else {
                return allocator_domain::absent;
            }
        }();

        consteval bool copy_preserves_swap_domain(std::meta::info type) {
            const auto allocator = std::meta::extract<allocator_domain>(
                std::meta::substitute(^^allocator_copy_domain, {type}));
            if (allocator != allocator_domain::absent) { return allocator == allocator_domain::preserved; }
            if (std::meta::is_array_type(type)) {
                return copy_preserves_swap_domain(std::meta::remove_extent(type));
            }
            if (std::meta::is_class_type(type)) {
                for (auto entity: reflected_entities(type)) {
                    if (!copy_preserves_swap_domain(std::meta::remove_cvref(std::meta::type_of(entity)))) {
                        return false;
                    }
                }
                return true;
            }
            return !std::meta::is_union_type(type);
        }

        consteval std::size_t initializer_count(std::meta::info type) {
            std::size_t count = 1;
            while (std::meta::is_array_type(type)) {
                count *= std::meta::extent(type);
                type = std::meta::remove_extent(type);
            }
            return count;
        }

        struct aggregate_initializer {
            std::meta::info entity;
            std::size_t array_index;
        };

        consteval auto aggregate_initializers(std::meta::info type) {
            std::vector<aggregate_initializer> result;
            for (auto entity: reflected_entities(type)) {
                const auto count = initializer_count(std::meta::remove_cvref(std::meta::type_of(entity)));
                for (std::size_t index = 0; index != count; ++index) {
                    result.push_back({entity, index});
                }
            }
            return std::define_static_array(result);
        }

        template<typename T>
        auto clone_initializer(const T &value, std::size_t index) {
            if constexpr (std::meta::is_array_type(^^T)) {
                constexpr auto stride = initializer_count(std::meta::remove_extent(^^T));
                return clone_initializer(value[index / stride], index % stride);
            } else {
                return transaction_traits<T>::clone(value);
            }
        }

        template<const auto &Initializers, std::size_t Index, typename T>
        auto clone_aggregate_initializer(const T &value) {
            constexpr auto entry = Initializers[Index];
            constexpr auto kind = std::meta::is_base(entry.entity)
                ? reflected_entity_kind::base : reflected_entity_kind::member;
            constexpr auto array_index = entry.array_index;
            return clone_initializer(entity_reference<entry.entity, kind>(value), array_index);
        }

        template<typename T>
        T clone_aggregate(const T &value) {
            static constexpr auto initializers = aggregate_initializers(^^T);
            // Brace elision initializes native array leaves without default construction
            // or assignment, both of which could select the wrong allocator.
            return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
                return T{clone_aggregate_initializer<initializers, Indices>(value)...};
            }(std::make_index_sequence<initializers.size()>{});
        }
    }

    template<typename T>
    struct transaction_traits {
        [[nodiscard]] static T clone(const T &value)
            requires std::copy_constructible<T> {
            if constexpr (requires { value.get_allocator(); T(value, value.get_allocator()); }) {
                return T(value, value.get_allocator());
            } else if constexpr (detail::copy_preserves_swap_domain(^^T)) {
                return T(value);
            } else {
                static_assert(std::meta::is_aggregate_type(^^T),
                    "allocator-bearing non-aggregate requires transaction_traits<T>");
                return detail::clone_aggregate(value);
            }
        }

        static void commit(T &out, T &staged) noexcept
            requires (detail::transaction_commit_supported(^^T)) {
            if constexpr (std::meta::is_nothrow_swappable_type(^^T)) {
                std::ranges::swap(out, staged);
            } else {
                template for (constexpr auto entity: std::define_static_array(detail::reflected_entities(^^T))) {
                    constexpr auto kind = std::meta::is_base(entity)
                        ? detail::reflected_entity_kind::base : detail::reflected_entity_kind::member;
                    using member = typename[:std::meta::remove_cvref(std::meta::type_of(entity)):];
                    transaction_traits<member>::commit(detail::entity_reference<entity, kind>(out),
                        detail::entity_reference<entity, kind>(staged));
                }
            }
        }
    };

    template<typename T, std::size_t Size>
    struct transaction_traits<T[Size]> {
        static void commit(T (&out)[Size], T (&staged)[Size]) noexcept
            requires requires(T &left, T &right) { { transaction_traits<T>::commit(left, right) } noexcept; } {
            for (std::size_t index = 0; index != Size; ++index) {
                transaction_traits<T>::commit(out[index], staged[index]);
            }
        }
    };

    template<typename T, std::size_t Size>
    struct transaction_traits<std::array<T, Size>> {
        [[nodiscard]] static std::array<T, Size> clone(const std::array<T, Size> &value) {
            return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
                return std::array<T, Size>{transaction_traits<T>::clone(value[Indices])...};
            }(std::make_index_sequence<Size>{});
        }

        static void commit(std::array<T, Size> &out, std::array<T, Size> &staged) noexcept
            requires requires(T &left, T &right) { { transaction_traits<T>::commit(left, right) } noexcept; } {
            for (std::size_t index = 0; index != Size; ++index) {
                transaction_traits<T>::commit(out[index], staged[index]);
            }
        }
    };

    template<typename T>
    concept transactional = std::copy_constructible<T> && requires(const T &source, T &out, T &staged) {
        { transaction_traits<T>::clone(source) } -> std::same_as<T>;
        { transaction_traits<T>::commit(out, staged) } noexcept;
    };
}
