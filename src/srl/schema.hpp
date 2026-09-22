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

#include "codec/reflected.hpp"
#include "container.hpp"

namespace srl {
    template<typename T>
    consteval bool has_serialized_fields() {
        using object_type = codec_value_type<T>;
        if constexpr (!std::is_class_v<object_type>) {
            return false;
        } else {
            return detail::has_field_annotations<object_type>();
        }
    }

    template<typename T>
    concept reflected_record = has_serialized_fields<T>()
                               && detail::atomic_annotation_count<codec_value_type<T>>() == 0;

    template<typename T>
    concept container_serializable = detail::adapted_container<codec_value_type<T>>;

    template<typename T>
    concept serializable = atomic_serializable<codec_value_type<T>>
                           || reflected_record<codec_value_type<T>>
                           || container_serializable<codec_value_type<T>>;

    template<typename T>
    consteval bool schema_valid();

    namespace detail {
        template<typename T>
        consteval bool atomic_metadata_valid() {
            if constexpr (atomic_annotation_count<T>() != 1 || !atomic_serializable<T>) {
                return false;
            } else if constexpr (!std::meta::bases_of(
                                     ^^T,
                                     std::meta::access_context::unchecked()).empty()) {
                return false;
            } else {
                bool valid = true;
                if constexpr (srl::atomic_uses_direct_object_representation<T>()
                              && !atomic_options<T>().bypass_pointer_check
                              && contains_pointer_representation<T>()) {
                    valid = false;
                }
                for_each_atomic_member<T>(
                    [&]<std::meta::info Member> {
                        using declared_type = reflected_entity_type_t<Member>;
                        if constexpr (std::meta::is_bit_field(Member)
                                      || std::is_reference_v<declared_type>
                                      || !std::meta::annotations_of_with_type(Member, ^^tag::field).empty()
                                      || !std::meta::annotations_of_with_type(Member, ^^tag::atomic).empty()
                                      || conversion_count<Member>() != 0) {
                            valid = false;
                        }
                    });
                return valid;
            }
        }

        template<typename T, typename... Ancestors>
        consteval bool schema_valid_impl();

        template<typename T, typename... Ancestors>
        consteval bool container_schema_valid() {
            using traits = container_traits<T>;
            if constexpr (traits::mapping_group_size == 1) {
                return schema_valid_impl<typename traits::element_type, Ancestors...>()
                       && (detail::fixed_container<T>
                           || std::default_initializable<typename traits::element_type>);
            } else {
                return schema_valid_impl<typename traits::key_type, Ancestors...>()
                       && schema_valid_impl<typename traits::mapped_type, Ancestors...>()
                       && std::default_initializable<typename traits::key_type>
                       && std::default_initializable<typename traits::mapped_type>;
            }
        }
    }

    template<typename T, typename... Ancestors>
    consteval bool detail::schema_valid_impl() {
        using object_type = codec_value_type<T>;
        if constexpr ((std::same_as<object_type, Ancestors> || ...)) {
            return false;
        } else if constexpr (detail::atomic_annotation_count<object_type>() != 0) {
            return detail::atomic_metadata_valid<object_type>();
        } else if constexpr (atomic_serializable<object_type>) {
            return true;
        } else if constexpr (container_serializable<object_type>) {
            return detail::container_schema_valid<object_type, Ancestors..., object_type>();
        } else if constexpr (!reflected_record<object_type>) {
            return false;
        } else {
            bool valid = true;
            std::vector<id_type> ids;
            detail::for_each_reflected_entity<object_type>(
                [&]<std::meta::info Entity, detail::reflected_entity_kind Kind> {
                    static constexpr auto annotations = std::define_static_array(
                        std::meta::annotations_of_with_type(Entity, ^^tag::field));
                    static constexpr auto atomic_annotations = std::define_static_array(
                        std::meta::annotations_of_with_type(Entity, ^^tag::atomic));
                    static constexpr auto container_annotations = std::define_static_array(
                        std::meta::annotations_of_with_type(Entity, ^^tag::container));
                    constexpr auto conversions = detail::conversion_count<Entity>();
                    if constexpr (annotations.size() > 1 || atomic_annotations.size() > 1
                                  || conversions > 1) {
                        valid = false;
                    } else if constexpr (conversions == 1
                                         && (annotations.size() != 1
                                             || atomic_annotations.size() != 0
                                             || Kind != detail::reflected_entity_kind::member)) {
                        valid = false;
                    } else if constexpr (atomic_annotations.size() == 1
                                         && (annotations.size() != 1
                                             || container_annotations.size() != 0
                                             || Kind != detail::reflected_entity_kind::member
                                             || !detail::field_uses_forced_atomic_representation<Entity>())) {
                        valid = false;
                    } else if constexpr (container_annotations.size() > 1
                                         || (container_annotations.size() == 1
                                             && (annotations.size() != 1
                                                 || !container_serializable<
                                                     detail::field_storage_t<Entity>>))) {
                        valid = false;
                    } else if constexpr (annotations.size() == 1) {
                        static constexpr auto annotation =
                            std::meta::extract<tag::field>(annotations.front());
                        using declared_type = detail::reflected_entity_type_t<Entity>;
                        using value_type = detail::reflected_entity_value_t<Entity>;
                        ids.push_back(annotation.id);
                        if constexpr (Kind == detail::reflected_entity_kind::member
                                      && std::meta::is_bit_field(Entity)) {
                            valid = false;
                        } else if constexpr (Kind == detail::reflected_entity_kind::base
                                             && std::meta::is_virtual(Entity)) {
                            valid = false;
                        } else if constexpr (conversions == 1) {
                            constexpr auto conversion = detail::conversion_options<Entity>();
                            using storage_type = detail::field_storage_t<Entity>;
                            if constexpr (!std::meta::is_type(conversion.target_type)
                                          || std::is_reference_v<declared_type>
                                          || std::is_const_v<declared_type>
                                          || std::is_volatile_v<declared_type>
                                          || !std::same_as<storage_type, codec_value_type<storage_type>>
                                          || !std::default_initializable<storage_type>
                                          || !std::move_constructible<storage_type>
                                          || !std::is_move_assignable_v<value_type>) {
                                valid = false;
                            } else {
                                if constexpr (conversion.handler_id == nullptr) {
                                    if constexpr (!requires(const value_type &mem, storage_type &&srl,
                                                            value_type &out) {
                                        static_cast<storage_type>(mem);
                                        out = static_cast<value_type>(std::move(srl));
                                    }) {
                                        valid = false;
                                    }
                                }
                                valid = valid && detail::schema_valid_impl<
                                    storage_type, Ancestors..., object_type>();
                            }
                        } else if constexpr (atomic_annotations.size() == 1) {
                            valid = valid
                                    && detail::field_uses_forced_atomic_representation<Entity>();
                        } else if constexpr (std::is_reference_v<declared_type>
                                              || std::is_pointer_v<value_type>) {
                            valid = false;
                        } else {
                            valid = valid && detail::schema_valid_impl<
                                value_type, Ancestors..., object_type>();
                        }
                    }
                });

            std::ranges::sort(ids);
            for (std::size_t index = 1; index < ids.size(); ++index) {
                if (ids[index - 1] == ids[index]) {
                    valid = false;
                }
            }
            return valid;
        }
    }

    template<typename T>
    consteval bool schema_valid() {
        return detail::schema_valid_impl<T>();
    }

    template<typename T>
    consteval bool requires_context() {
        static_assert(schema_valid<T>(), "invalid serialization schema");
        using value_type = codec_value_type<T>;
        if constexpr (atomic_serializable<value_type>) {
            return false;
        } else if constexpr (container_serializable<value_type>) {
            using traits = container_traits<value_type>;
            if constexpr (traits::mapping_group_size == 1) {
                return requires_context<typename traits::element_type>();
            } else {
                return requires_context<typename traits::key_type>()
                       || requires_context<typename traits::mapped_type>();
            }
        } else {
            bool result = false;
            detail::for_each_serialized_entity<value_type>(
                [&]<std::meta::info Entity, detail::reflected_entity_kind>(tag::field) {
                    if constexpr (detail::conversion_count<Entity>() == 1) {
                        result |= detail::conversion_options<Entity>().handler_id != nullptr;
                    }
                    if constexpr (detail::field_atomic_annotation_count<Entity>() == 0) {
                        result |= requires_context<detail::field_storage_t<Entity>>();
                    }
                });
            return result;
        }
    }

    template<typename T>
    consteval std::size_t serialized_field_count() {
        std::size_t count{};
        detail::for_each_serialized_entity<codec_value_type<T>>(
            [&]<std::meta::info, detail::reflected_entity_kind>(tag::field) {
                ++count;
            });
        return count;
    }
}

