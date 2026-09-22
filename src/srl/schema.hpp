#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <meta>
#include <span>
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
            return detail::auto_seq_annotation_count<object_type>() != 0
                   || detail::has_field_annotations<object_type>();
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
            if constexpr (atomic_annotation_count<T>() != 1
                          || auto_seq_annotation_count<T>() != 0
                          || bypass_pointer_check_annotation_count<T>() > 1
                          || !atomic_serializable<T>) {
                return false;
            } else {
                bool valid = true;
                detail::for_each_reflected_entity<T>(
                    [&]<std::meta::info Entity, reflected_entity_kind Kind> {
                        using declared_type = reflected_entity_type_t<Entity>;
                        constexpr auto transient_count = transient_annotation_count<Entity>();
                        constexpr auto bypass_count =
                            entity_bypass_pointer_check_annotation_count<Entity>();
                        if constexpr (transient_count > 1
                                      || (transient_count == 1
                                          && (Kind != reflected_entity_kind::member
                                              || !std::meta::annotations_of_with_type(Entity, ^^tag::field).empty()
                                              || !std::meta::annotations_of_with_type(Entity, ^^tag::atomic).empty()
                                              || !std::meta::annotations_of_with_type(Entity, ^^tag::container).empty()
                                              || bypass_count != 0
                                              || conversion_count<Entity>() != 0))) {
                            valid = false;
                        } else if constexpr (transient_count == 1) {
                            return;
                        } else if constexpr (bypass_count > 1
                                      || (Kind == reflected_entity_kind::base && std::meta::is_virtual(Entity))
                                      || (Kind == reflected_entity_kind::member && (std::meta::is_bit_field(Entity) || std::is_reference_v<declared_type>))
                                      || !std::meta::annotations_of_with_type(Entity, ^^tag::field).empty()
                                      || !std::meta::annotations_of_with_type(Entity, ^^tag::atomic).empty()
                                      || !std::meta::annotations_of_with_type(Entity, ^^tag::container).empty()
                                      || conversion_count<Entity>() != 0) {
                            valid = false;
                        } else if constexpr (contains_pointer_representation<
                                                 reflected_entity_value_t<Entity>>()
                                             && bypass_pointer_check_annotation_count<T>() == 0
                                             && bypass_count == 0) {
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
        } else if constexpr (detail::auto_seq_annotation_count<object_type>() > 1) {
            return false;
        } else if constexpr (detail::bypass_pointer_check_annotation_count<object_type>() > 1
                             || (detail::bypass_pointer_check_annotation_count<object_type>() != 0
                                 && detail::auto_seq_annotation_count<object_type>() != 1)) {
            return false;
        } else {
            bool valid = true;
            std::vector<id_type> ids;
            detail::for_each_reflected_entity<object_type>(
                [&]<std::meta::info Entity, reflected_entity_kind Kind> {
                    constexpr bool auto_sequence =
                        detail::auto_seq_annotation_count<object_type>() == 1;
                    static constexpr auto annotations = std::define_static_array(
                        std::meta::annotations_of_with_type(Entity, ^^tag::field));
                    static constexpr auto atomic_annotations = std::define_static_array(
                        std::meta::annotations_of_with_type(Entity, ^^tag::atomic));
                    static constexpr auto container_annotations = std::define_static_array(
                        std::meta::annotations_of_with_type(Entity, ^^tag::container));
                    constexpr auto transient_count = detail::transient_annotation_count<Entity>();
                    constexpr auto bypass_count =
                        detail::entity_bypass_pointer_check_annotation_count<Entity>();
                    constexpr auto conversions = detail::conversion_count<Entity>();
                    constexpr bool serialized = auto_sequence || annotations.size() == 1;
                    if constexpr (transient_count > 1
                                  || (transient_count == 1
                                      && (!auto_sequence
                                          || annotations.size() != 0
                                          || atomic_annotations.size() != 0
                                          || container_annotations.size() != 0
                                          || bypass_count != 0
                                          || conversions != 0))) {
                        valid = false;
                    } else if constexpr (transient_count == 1) {
                        return;
                    } else if constexpr (annotations.size() > 1
                                  || atomic_annotations.size() > 1
                                  || bypass_count > 1
                                  || conversions > 1) {
                        valid = false;
                    } else if constexpr (conversions == 1
                                         && (!serialized
                                             || atomic_annotations.size() != 0
                                             || bypass_count != 0
                                             || Kind != reflected_entity_kind::member)) {
                        valid = false;
                    } else if constexpr (atomic_annotations.size() == 1
                                         && (!serialized
                                             || container_annotations.size() != 0
                                             || !detail::entity_uses_forced_object_representation<
                                                 object_type, Entity>())) {
                        valid = false;
                    } else if constexpr (container_annotations.size() > 1
                                         || (container_annotations.size() == 1
                                             && (!serialized
                                                 || bypass_count != 0
                                                 || !container_serializable<field_storage_t<Entity>>))) {
                        valid = false;
                    } else if constexpr (bypass_count == 1
                                         && !auto_sequence
                                         && atomic_annotations.size() == 0) {
                        valid = false;
                    } else if constexpr (serialized) {
                        using declared_type = reflected_entity_type_t<Entity>;
                        using value_type = reflected_entity_value_t<Entity>;
                        if constexpr (Kind == reflected_entity_kind::member
                                      && std::meta::is_bit_field(Entity)) {
                            valid = false;
                        } else if constexpr (Kind == reflected_entity_kind::base
                                             && std::meta::is_virtual(Entity)) {
                            valid = false;
                        } else if constexpr (conversions == 1) {
                            constexpr auto conversion = detail::conversion_options<Entity>();
                            using storage_type = field_storage_t<Entity>;
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
                        } else if constexpr (detail::entity_uses_forced_object_representation<
                                                 object_type, Entity>()) {
                            valid = valid
                                    && detail::entity_uses_forced_object_representation<
                                        object_type, Entity>();
                        } else if constexpr (std::is_reference_v<declared_type>
                                              || std::is_pointer_v<value_type>) {
                            valid = false;
                        } else {
                            valid = valid && detail::schema_valid_impl<
                                value_type, Ancestors..., object_type>();
                        }
                    }
                });

            if constexpr (detail::auto_seq_annotation_count<object_type>() == 1) {
                valid = valid && detail::auto_seq_ids_representable<object_type>();
            }
            detail::for_each_serialized_entity<object_type>(
                [&]<std::meta::info, reflected_entity_kind>(tag::field annotation) {
                    ids.push_back(annotation.id);
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
    concept schemaed = detail::schema_valid_impl<T>();

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
