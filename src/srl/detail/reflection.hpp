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

#include "../annotations.hpp"
#include "../codec/base.hpp"
#include "../context.hpp"

namespace srl {
    namespace detail {
        enum class reflected_entity_kind : std::uint8_t {
            member = 1,
            base = 2,
        };

        template<std::meta::info Entity>
        using reflected_entity_type_t = typename[:std::meta::type_of(Entity):];

        template<std::meta::info Entity>
        using reflected_entity_value_t = std::remove_cvref_t<reflected_entity_type_t<Entity>>;

        template<std::meta::info Entity>
        consteval std::size_t conversion_count() {
            return std::meta::annotations_of_with_type(Entity, ^^conversion_tag).size();
        }

        template<std::meta::info Entity>
        consteval conversion_tag conversion_options() {
            static_assert(conversion_count<Entity>() == 1);
            return std::meta::extract<conversion_tag>(
                std::meta::annotations_of_with_type(Entity, ^^conversion_tag).front());
        }

        template<std::meta::info Entity>
        consteval std::meta::info field_storage_type() {
            if constexpr (conversion_count<Entity>() == 1) {
                constexpr auto target = conversion_options<Entity>().target_type;
                if constexpr (std::meta::is_type(target)) {
                    return target;
                }
            }
            return ^^reflected_entity_value_t<Entity>;
        }

        template<std::meta::info Entity>
        using field_storage_t = typename[:field_storage_type<Entity>():];

        template<typename T, typename Visitor>
        constexpr void for_each_reflected_entity(Visitor &&visitor) {
            static constexpr auto members = std::define_static_array(
                std::meta::nonstatic_data_members_of(
                    ^^T,
                    std::meta::access_context::unchecked()));
            template for (constexpr auto member: members) {
                visitor.template operator()<member, reflected_entity_kind::member>();
            }

            static constexpr auto bases = std::define_static_array(
                std::meta::bases_of(
                    ^^T,
                    std::meta::access_context::unchecked()));
            template for (constexpr auto base: bases) {
                visitor.template operator()<base, reflected_entity_kind::base>();
            }
        }

        template<typename T, typename Visitor>
        constexpr void for_each_serialized_entity(Visitor &&visitor) {
            detail::for_each_reflected_entity<T>(
                [&]<std::meta::info Entity, reflected_entity_kind Kind> {
                    static constexpr auto annotations = std::define_static_array(
                        std::meta::annotations_of_with_type(Entity, ^^tag::field));
                    if constexpr (annotations.size() == 1) {
                        static constexpr auto annotation =
                            std::meta::extract<tag::field>(annotations.front());
                        visitor.template operator()<Entity, Kind>(annotation);
                    }
                });
        }

        template<typename T, typename Visitor>
        constexpr void for_each_atomic_member(Visitor &&visitor) {
            static constexpr auto members = std::define_static_array(
                std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));

            template for (constexpr auto member: members) {
                visitor.template operator()<member>();
            }
        }

        template<std::meta::info Entity, reflected_entity_kind Kind, typename Object>
        [[nodiscard]] constexpr decltype(auto) entity_reference(Object &object) noexcept {
            if constexpr (Kind == reflected_entity_kind::member) {
                return (object.[:Entity:]);
            } else {
                using base_type = reflected_entity_value_t<Entity>;
                if constexpr (std::is_const_v<Object>) {
                    return static_cast<const base_type &>(object);
                } else {
                    return static_cast<base_type &>(object);
                }
            }
        }

        template<std::meta::info Entity, typename Object>
        [[nodiscard]] constexpr decltype(auto) member_reference(Object &object) noexcept {
            return (object.[:Entity:]);
        }

        template<typename T>
        consteval std::size_t atomic_annotation_count() {
            if constexpr (!std::is_class_v<T>) {
                return 0;
            } else {
                return std::meta::annotations_of_with_type(^^T, ^^tag::atomic).size();
            }
        }

        template<typename T>
        consteval tag::atomic atomic_options() {
            static_assert(atomic_annotation_count<T>() == 1);
            static constexpr auto annotations = std::define_static_array(
                std::meta::annotations_of_with_type(^^T, ^^tag::atomic));
            return std::meta::extract<tag::atomic>(annotations.front());
        }

        template<std::meta::info Entity>
        consteval std::size_t field_atomic_annotation_count() {
            return std::meta::annotations_of_with_type(Entity, ^^tag::atomic).size();
        }

        template<std::meta::info Entity>
        consteval tag::atomic field_atomic_options() {
            static_assert(field_atomic_annotation_count<Entity>() == 1);
            static constexpr auto annotations = std::define_static_array(
                std::meta::annotations_of_with_type(Entity, ^^tag::atomic));
            return std::meta::extract<tag::atomic>(annotations.front());
        }

        template<typename T>
        consteval bool contains_pointer_representation() {
            using value_type = std::remove_cv_t<T>;
            if constexpr (std::is_pointer_v<value_type>
                          || std::is_member_pointer_v<value_type>) {
                return true;
            } else if constexpr (std::is_array_v<value_type>) {
                return contains_pointer_representation<std::remove_extent_t<value_type>>();
            } else if constexpr (std::is_class_v<value_type>
                                 || std::is_union_v<value_type>) {
                bool found = false;
                for_each_reflected_entity<value_type>(
                    [&]<std::meta::info Entity, reflected_entity_kind> {
                        using declared_type = reflected_entity_type_t<Entity>;
                        if constexpr (std::is_reference_v<declared_type>
                                      || contains_pointer_representation<
                                          std::remove_cvref_t<declared_type>>()) {
                            found = true;
                        }
                    });
                return found;
            } else {
                return false;
            }
        }

        template<typename T>
        consteval bool direct_atomic_representation_supported(tag::atomic options) {
            return std::is_trivially_copyable_v<T>
                   && (options.allow_padding
                       || std::has_unique_object_representations_v<T>)
                   && (options.bypass_pointer_check
                       || !contains_pointer_representation<T>());
        }

        template<typename T>
        consteval bool atomic_propagates_impl() {
            using value_type = codec_value_type<T>;
            if constexpr (atomic_annotation_count<value_type>() == 1) {
                return atomic_options<value_type>().propagate;
            } else {
                return atomic_serializable<value_type>;
            }
        }

        template<typename T>
        consteval bool atomic_uses_direct_object_representation_impl() {
            static_assert(atomic_annotation_count<T>() == 1);
            return atomic_options<T>().allow_padding
                   || std::has_unique_object_representations_v<T>;
        }

        template<std::meta::info Entity>
        consteval bool field_uses_forced_atomic_representation() {
            if constexpr (field_atomic_annotation_count<Entity>() != 1) {
                return false;
            } else {
                using declared_type = reflected_entity_type_t<Entity>;
                using value_type = reflected_entity_value_t<Entity>;
                return !std::is_reference_v<declared_type>
                       && !std::meta::is_bit_field(Entity)
                       && direct_atomic_representation_supported<value_type>(
                           field_atomic_options<Entity>());
            }
        }

        template<typename T>
        consteval bool has_field_annotations() {
            if constexpr (!std::is_class_v<T>) {
                return false;
            } else {
                bool found = false;
                detail::for_each_reflected_entity<T>(
                    [&]<std::meta::info Entity, reflected_entity_kind> {
                        if constexpr (!std::meta::annotations_of_with_type(Entity, ^^tag::field).empty()) {
                            found = true;
                        }
                    });
                return found;
            }
        }

        template<typename T>
        consteval bool auto_atomic_supported() {
            if constexpr (!std::is_class_v<T>) {
                return false;
            } else if constexpr (atomic_annotation_count<T>() != 1) {
                return false;
            } else if constexpr (!std::meta::bases_of(
                                     ^^T,
                                     std::meta::access_context::unchecked()).empty()) {
                return false;
            } else if constexpr (atomic_uses_direct_object_representation_impl<T>()
                                 && !std::is_trivially_copyable_v<T>) {
                return false;
            } else if constexpr (atomic_uses_direct_object_representation_impl<T>()
                                 && !atomic_options<T>().bypass_pointer_check
                                 && contains_pointer_representation<T>()) {
                return false;
            } else {
                bool valid = true;
                for_each_atomic_member<T>(
                    [&]<std::meta::info Member> {
                        using declared_type = reflected_entity_type_t<Member>;
                        using value_type = reflected_entity_value_t<Member>;
                        if constexpr (std::meta::is_bit_field(Member)
                                      || std::is_reference_v<declared_type>
                                      || !std::meta::annotations_of_with_type(Member, ^^tag::field).empty()
                                      || !std::meta::annotations_of_with_type(Member, ^^tag::atomic).empty()
                                      || conversion_count<Member>() != 0) {
                            valid = false;
                        } else if constexpr (atomic_uses_direct_object_representation_impl<T>()) {
                            if constexpr (std::is_pointer_v<value_type>) {
                                if constexpr (!atomic_options<T>().bypass_pointer_check) {
                                    valid = false;
                                }
                            } else if constexpr (!atomic_serializable<value_type>
                                                 || !atomic_propagates_impl<value_type>()) {
                                valid = false;
                            }
                        } else {
                            if constexpr (std::is_pointer_v<value_type>
                                          || !atomic_serializable<value_type>
                                          || !atomic_propagates_impl<value_type>()) {
                                valid = false;
                            } else if constexpr (atomic_codec<value_type>::fixed_size
                                                 == std::dynamic_extent) {
                                valid = false;
                            }
                        }
                    });
                return valid;
            }
        }
    }
}

