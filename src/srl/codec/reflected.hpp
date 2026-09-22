#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <meta>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "base.hpp"
#include "../detail/reflection.hpp"

namespace srl {
    template<typename T>
    consteval bool atomic_propagates() {
        return detail::atomic_propagates_impl<codec_value_type<T>>();
    }

    template<typename T>
    consteval bool atomic_uses_direct_object_representation() {
        using value_type = codec_value_type<T>;
        if constexpr (!atomic_serializable<value_type>) {
            return false;
        } else if constexpr (requires {
                                 atomic_codec<value_type>::uses_direct_object_representation;
                             }) {
            return atomic_codec<value_type>::uses_direct_object_representation;
        } else {
            return false;
        }
    }

    template<typename T>
        requires (detail::auto_atomic_supported<T>())
    struct atomic_codec<T> {
        static constexpr bool uses_direct_object_representation =
            detail::atomic_uses_direct_object_representation_impl<T>();

        static constexpr std::size_t fixed_size = [] consteval {
            if constexpr (uses_direct_object_representation) {
                return sizeof(T);
            } else {
                std::size_t result{};
                detail::for_each_atomic_member<T>(
                    [&]<std::meta::info Member> {
                        using value_type = detail::reflected_entity_value_t<Member>;
                        result += atomic_codec<value_type>::fixed_size;
                    });
                return result;
            }
        }();

        [[nodiscard]] static constexpr std::expected<std::size_t, error> encoded_size(
            const T &) noexcept {
            return fixed_size;
        }

        [[nodiscard]] static std::expected<void, error> encode(
            const T &value,
            std::span<std::byte> destination) {
            if (destination.size() != fixed_size) {
                return detail::fail(error_code::invalid_payload_shape);
            }
            if constexpr (uses_direct_object_representation) {
                std::memcpy(destination.data(), &value, sizeof(T));
                return {};
            } else {
                std::size_t offset{};
                std::expected<void, error> status{};
                detail::for_each_atomic_member<T>(
                    [&]<std::meta::info Member> {
                        if (!status) {
                            return;
                        }
                        using value_type = detail::reflected_entity_value_t<Member>;
                        constexpr auto member_size = atomic_codec<value_type>::fixed_size;
                        status = atomic_codec<value_type>::encode(
                            detail::member_reference<Member>(value),
                            destination.subspan(offset, member_size));
                        offset += member_size;
                    });
                return status;
            }
        }

        [[nodiscard]] static std::expected<void, error> decode(
            std::span<const std::byte> source,
            T &out) {
            if (source.size() != fixed_size) {
                return detail::fail(error_code::invalid_payload_shape);
            }
            if constexpr (uses_direct_object_representation) {
                std::memcpy(&out, source.data(), sizeof(T));
                return {};
            } else {
                std::size_t offset{};
                std::expected<void, error> status{};
                detail::for_each_atomic_member<T>(
                    [&]<std::meta::info Member> {
                        if (!status) {
                            return;
                        }
                        using value_type = detail::reflected_entity_value_t<Member>;
                        constexpr auto member_size = atomic_codec<value_type>::fixed_size;
                        status = atomic_codec<value_type>::decode(
                            source.subspan(offset, member_size),
                            detail::member_reference<Member>(out));
                        offset += member_size;
                    });
                return status;
            }
        }
    };
}

