#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <meta>
#include <ranges>
#include <type_traits>
#include <utility>

#include "detail/reflection.hpp"

namespace srl {
    // A customization owns both construction and commit: clone must preserve
    // every precondition of commit, including allocator equality.
    template<typename T>
    struct transaction_traits;

    namespace detail {
        template<typename T>
        concept has_adl_swap = requires(T &left, T &right) { swap(left, right); };

        template<typename T>
        consteval bool transaction_commit_supported() {
            if constexpr (std::is_nothrow_swappable_v<T>) {
                return true;
            } else if constexpr (std::is_aggregate_v<T> && !has_adl_swap<T> && !std::is_array_v<T>) {
                bool supported = true;
                for_each_reflected_entity<T>([&]<std::meta::info Entity, reflected_entity_kind> {
                    using member = reflected_entity_value_t<Entity>;
                    supported &= requires(member &left, member &right) {
                        { transaction_traits<member>::commit(left, right) } noexcept;
                    };
                });
                return supported;
            } else {
                return false;
            }
        }

        template<typename T>
        consteval bool copy_preserves_swap_domain() {
            if constexpr (requires { typename T::allocator_type; }) {
                using traits = std::allocator_traits<typename T::allocator_type>;
                return traits::is_always_equal::value || traits::propagate_on_container_swap::value;
            } else if constexpr (std::is_array_v<T>) {
                return copy_preserves_swap_domain<std::remove_extent_t<T>>();
            } else if constexpr (std::is_class_v<T>) {
                bool safe = true;
                for_each_reflected_entity<T>([&]<std::meta::info Entity, reflected_entity_kind> {
                    safe &= copy_preserves_swap_domain<reflected_entity_value_t<Entity>>();
                });
                return safe;
            } else {
                return !std::is_union_v<T>;
            }
        }

        template<typename T>
        consteval auto transaction_entities() {
            auto result = std::meta::bases_of(^^T, std::meta::access_context::unchecked());
            for (auto member: std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                result.push_back(member);
            }
            return std::define_static_array(result);
        }

        template<typename T>
        consteval std::size_t initializer_count() {
            if constexpr (std::is_array_v<T>) {
                return std::extent_v<T> * initializer_count<std::remove_extent_t<T>>();
            } else {
                return 1;
            }
        }

        template<std::size_t Index, typename T>
        auto clone_initializer(const T &value) {
            if constexpr (std::is_array_v<T>) {
                constexpr auto stride = initializer_count<std::remove_extent_t<T>>();
                return clone_initializer<Index % stride>(value[Index / stride]);
            } else {
                return transaction_traits<T>::clone(value);
            }
        }

        template<typename T>
        consteval std::size_t aggregate_initializer_count() {
            std::size_t count{};
            template for (constexpr auto entity: transaction_entities<T>()) {
                count += initializer_count<reflected_entity_value_t<entity>>();
            }
            return count;
        }

        template<typename T, std::size_t Index, std::size_t EntityIndex = 0, std::size_t Begin = 0>
        auto clone_aggregate_initializer(const T &value) {
            constexpr auto entity = transaction_entities<T>()[EntityIndex];
            constexpr auto count = initializer_count<reflected_entity_value_t<entity>>();
            if constexpr (Index < Begin + count) {
                constexpr auto kind = std::meta::is_base(entity)
                    ? reflected_entity_kind::base : reflected_entity_kind::member;
                return clone_initializer<Index - Begin>(entity_reference<entity, kind>(value));
            } else {
                return clone_aggregate_initializer<T, Index, EntityIndex + 1, Begin + count>(value);
            }
        }

        template<typename T, std::size_t... Indices>
        T clone_aggregate(const T &value, std::index_sequence<Indices...>) {
            // Brace elision initializes native array leaves without default construction
            // or assignment, both of which could select the wrong allocator.
            return T{clone_aggregate_initializer<T, Indices>(value)...};
        }
    }

    template<typename T>
    struct transaction_traits {
        [[nodiscard]] static T clone(const T &value)
            requires std::copy_constructible<T> {
            if constexpr (requires { value.get_allocator(); T(value, value.get_allocator()); }) {
                return T(value, value.get_allocator());
            } else if constexpr (detail::copy_preserves_swap_domain<T>()) {
                return T(value);
            } else {
                static_assert(std::is_aggregate_v<T>,
                    "allocator-bearing non-aggregate requires transaction_traits<T>");
                return detail::clone_aggregate(value,
                    std::make_index_sequence<detail::aggregate_initializer_count<T>()>{});
            }
        }

        static void commit(T &out, T &staged) noexcept
            requires (detail::transaction_commit_supported<T>()) {
            if constexpr (std::is_nothrow_swappable_v<T>) {
                std::ranges::swap(out, staged);
            } else {
                detail::for_each_reflected_entity<T>([&]<std::meta::info Entity, detail::reflected_entity_kind Kind> {
                    using member = detail::reflected_entity_value_t<Entity>;
                    transaction_traits<member>::commit(detail::entity_reference<Entity, Kind>(out),
                        detail::entity_reference<Entity, Kind>(staged));
                });
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
