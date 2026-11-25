#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <meta>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace xasrl::detail {
    template<typename T>
    struct default_snapshot {
        static constexpr bool valid = std::meta::is_structural_type(^^T)
            && std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>;
        using snapshot_type = T;

        static consteval snapshot_type capture(const T &value) requires valid {
            return value;
        }

        static void apply(T &out, const snapshot_type &value) requires valid {
            out = value;
        }
    };

    template<typename T, std::size_t N>
    struct default_snapshot<T[N]> {
        using element = default_snapshot<T>;
        static constexpr bool valid = element::valid;
        using snapshot_type = std::array<typename element::snapshot_type, N>;

        static consteval snapshot_type capture(const T (&value)[N]) requires valid {
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return snapshot_type{element::capture(value[I])...};
            }(std::make_index_sequence<N>{});
        }

        static void apply(T (&out)[N], const snapshot_type &value) requires valid {
            for (std::size_t i = 0; i != N; ++i) {
                element::apply(out[i], value[i]);
            }
        }
    };

    template<typename T, std::size_t N>
    struct default_snapshot<std::array<T, N>> {
        using element = default_snapshot<T>;
        static constexpr bool valid = element::valid;
        using snapshot_type = std::array<typename element::snapshot_type, N>;

        static consteval snapshot_type capture(const std::array<T, N> &value) requires valid {
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return snapshot_type{element::capture(value[I])...};
            }(std::make_index_sequence<N>{});
        }

        static void apply(std::array<T, N> &out, const snapshot_type &value) requires valid {
            for (std::size_t i = 0; i != N; ++i) {
                element::apply(out[i], value[i]);
            }
        }
    };

    template<typename T>
    struct default_sequence_snapshot {
        const T *data;
        std::size_t size;
    };

    template<typename T, typename Range>
    consteval auto capture_default_sequence(const Range &value) {
        using element = default_snapshot<T>;
        std::vector<typename element::snapshot_type> values;
        values.reserve(value.size());
        for (const auto &item: value) {
            values.push_back(element::capture(item));
        }
        // Only the frozen structural elements survive constant evaluation,
        // never a vector's allocator-owned storage or an owner's subobject.
        const auto storage = std::define_static_array(values);
        return default_sequence_snapshot<typename element::snapshot_type>{storage.data(), storage.size()};
    }

    template<typename Char, typename Traits>
    struct default_snapshot<std::basic_string<Char, Traits, std::allocator<Char>>> {
        using value_type = std::basic_string<Char, Traits, std::allocator<Char>>;
        using element = default_snapshot<Char>;
        static constexpr bool valid = element::valid;
        using snapshot_type = default_sequence_snapshot<typename element::snapshot_type>;

        static consteval snapshot_type capture(const value_type &value) requires valid {
            return capture_default_sequence<Char>(value);
        }

        static void apply(value_type &out, const snapshot_type &value) requires valid {
            value_type restored(out.get_allocator());
            restored.resize(value.size);
            for (std::size_t i = 0; i != value.size; ++i) {
                element::apply(restored[i], value.data[i]);
            }
            out.swap(restored);
        }
    };

    template<typename T>
    struct default_snapshot<std::vector<T, std::allocator<T>>> {
        using value_type = std::vector<T, std::allocator<T>>;
        using element = default_snapshot<T>;
        static constexpr bool direct = std::same_as<T, typename element::snapshot_type>;
        static constexpr bool valid = element::valid
            && (direct || std::default_initializable<T>) && std::is_move_constructible_v<T>;
        using snapshot_type = default_sequence_snapshot<typename element::snapshot_type>;

        static consteval snapshot_type capture(const value_type &value) requires valid {
            return capture_default_sequence<T>(value);
        }

        static void apply(value_type &out, const snapshot_type &value) requires valid {
            value_type restored(out.get_allocator());
            restored.reserve(value.size);
            for (std::size_t i = 0; i != value.size; ++i) {
                if constexpr (direct) {
                    restored.push_back(value.data[i]);
                } else {
                    T item{};
                    element::apply(item, value.data[i]);
                    restored.push_back(std::move(item));
                }
            }
            out.swap(restored);
        }
    };
}
