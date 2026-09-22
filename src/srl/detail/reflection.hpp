#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <meta>
#include <span>
#include <type_traits>
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

        struct byte_range {
            std::size_t offset{};
            std::size_t size{};
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
            constexpr static auto count = conversion_count<Entity>();
            static_assert(count <= 1);
            if constexpr (count == 1) {
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
            static constexpr auto bases = std::define_static_array(
                std::meta::bases_of(
                    ^^T,
                    std::meta::access_context::unchecked()));
            template for (constexpr auto base: bases) {
                visitor.template operator()<base, reflected_entity_kind::base>();
            }

            static constexpr auto members = std::define_static_array(
                std::meta::nonstatic_data_members_of(
                    ^^T,
                    std::meta::access_context::unchecked()));
            template for (constexpr auto member: members) {
                visitor.template operator()<member, reflected_entity_kind::member>();
            }
        }

        template<std::meta::info Entity>
        consteval std::size_t transient_annotation_count() {
            return std::meta::annotations_of_with_type(Entity, ^^tag::transient).size();
        }

        template<typename T>
        consteval std::size_t auto_seq_annotation_count() {
            if constexpr (!std::is_class_v<T>) {
                return 0;
            } else {
                return std::meta::annotations_of_with_type(^^T, ^^tag::auto_seq).size();
            }
        }

        template<typename T, typename Visitor>
        constexpr void for_each_serialized_entity(Visitor &&visitor) {
            if constexpr (auto_seq_annotation_count<T>() == 1) {
                std::uint64_t next_id{};
                detail::for_each_reflected_entity<T>(
                    [&]<std::meta::info Entity, reflected_entity_kind Kind> {
                        if constexpr (transient_annotation_count<Entity>() == 0) {
                            static constexpr auto annotations = std::define_static_array(
                                std::meta::annotations_of_with_type(Entity, ^^tag::field));
                            tag::field annotation{};
                            if constexpr (annotations.size() == 1) {
                                annotation = std::meta::extract<tag::field>(annotations.front());
                                next_id = annotation.id;
                            } else {
                                annotation.id = next_id <= std::numeric_limits<id_type>::max()
                                                    ? static_cast<id_type>(next_id)
                                                    : id_type{};
                            }
                            visitor.template operator()<Entity, Kind>(annotation);
                            ++next_id;
                        }
                    });
            } else {
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
        }

        template<typename T>
        consteval bool auto_seq_ids_representable() {
            if constexpr (auto_seq_annotation_count<T>() != 1) {
                return true;
            } else {
                std::uint64_t next_id{};
                bool valid = true;
                detail::for_each_reflected_entity<T>(
                    [&]<std::meta::info Entity, reflected_entity_kind> {
                        if constexpr (transient_annotation_count<Entity>() == 0) {
                            static constexpr auto annotations = std::define_static_array(
                                std::meta::annotations_of_with_type(Entity, ^^tag::field));
                            if constexpr (annotations.size() == 1) {
                                next_id = std::meta::extract<tag::field>(
                                    annotations.front()).id;
                            } else if (next_id > std::numeric_limits<id_type>::max()) {
                                valid = false;
                            }
                            ++next_id;
                        }
                    });
                return valid;
            }
        }

        template<typename T, typename Visitor>
        constexpr void for_each_atomic_entity(Visitor &&visitor) {
            detail::for_each_reflected_entity<T>(
                [&]<std::meta::info Entity, reflected_entity_kind Kind> {
                    if constexpr (transient_annotation_count<Entity>() == 0) {
                        visitor.template operator()<Entity, Kind>();
                    }
                });
        }

        template<typename T>
        consteval bool has_transient_members() {
            bool found = false;
            detail::for_each_reflected_entity<T>(
                [&]<std::meta::info Entity, reflected_entity_kind Kind> {
                    if constexpr (Kind == reflected_entity_kind::member
                                  && transient_annotation_count<Entity>() != 0) {
                        found = true;
                    }
                });
            return found;
        }

        template<typename T>
        consteval bool transient_layout_supports_segmented_copy() {
            bool valid = true;
            std::vector<byte_range> transient_ranges;
            detail::for_each_reflected_entity<T>(
                [&]<std::meta::info Entity, reflected_entity_kind Kind> {
                    if constexpr (transient_annotation_count<Entity>() == 1) {
                        if constexpr (Kind != reflected_entity_kind::member
                                      || std::meta::is_bit_field(Entity)) {
                            valid = false;
                        } else {
                            constexpr auto offset = std::meta::offset_of(Entity);
                            if constexpr (offset.bits != 0) {
                                valid = false;
                            } else {
                                transient_ranges.push_back({
                                    static_cast<std::size_t>(offset.bytes),
                                    std::meta::size_of(Entity),
                                });
                            }
                        }
                    }
                });
            if (!valid) {
                return false;
            }

            detail::for_each_reflected_entity<T>(
                [&]<std::meta::info Entity, reflected_entity_kind> {
                    if constexpr (transient_annotation_count<Entity>() == 0) {
                        constexpr auto offset = std::meta::offset_of(Entity);
                        constexpr auto begin = static_cast<std::size_t>(offset.bytes);
                        constexpr auto end = begin + std::meta::size_of(Entity);
                        if constexpr (offset.bits != 0) {
                            valid = false;
                        } else {
                            for (const auto transient: transient_ranges) {
                                const auto transient_end = transient.offset + transient.size;
                                if (begin < transient_end && transient.offset < end) {
                                    valid = false;
                                }
                            }
                        }
                    }
                });
            return valid;
        }

        template<typename T>
        consteval auto atomic_copy_ranges() {
            std::vector<byte_range> excluded;
            detail::for_each_reflected_entity<T>(
                [&]<std::meta::info Entity, reflected_entity_kind> {
                    if constexpr (transient_annotation_count<Entity>() == 1) {
                        constexpr auto offset = std::meta::offset_of(Entity);
                        excluded.push_back({
                            static_cast<std::size_t>(offset.bytes),
                            std::meta::size_of(Entity),
                        });
                    }
                });
            std::ranges::sort(excluded, {}, &byte_range::offset);

            std::vector<byte_range> result;
            std::size_t begin{};
            for (const auto range: excluded) {
                if (begin < range.offset) {
                    result.push_back({begin, range.offset - begin});
                }
                begin = std::max(begin, range.offset + range.size);
            }
            if (begin < sizeof(T)) {
                result.push_back({begin, sizeof(T) - begin});
            }
            return std::define_static_array(result);
        }

        template<std::meta::info Entity, reflected_entity_kind Kind, typename Object>
        [[nodiscard]] constexpr decltype(auto) entity_reference(Object &object) noexcept {
            if constexpr (Kind == reflected_entity_kind::member) {
                return (object.[:Entity:]);
            } else {
                using base_type = reflected_entity_value_t<Entity>;
                if constexpr (std::is_const_v<Object>) {
                    return (const base_type &)object;
                } else {
                    return (base_type &)object;
                }
            }
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
        consteval std::size_t bypass_pointer_check_annotation_count() {
            if constexpr (!std::is_class_v<T>) {
                return 0;
            } else {
                return std::meta::annotations_of_with_type(
                    ^^T, ^^tag::bypass_pointer_check).size();
            }
        }

        template<std::meta::info Entity>
        consteval std::size_t entity_bypass_pointer_check_annotation_count() {
            return std::meta::annotations_of_with_type(
                Entity, ^^tag::bypass_pointer_check).size();
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
        consteval bool direct_atomic_representation_supported(
            tag::atomic options, bool bypass_pointer_check) {
            return std::is_trivially_copyable_v<T>
                   && (options.allow_padding
                       || std::has_unique_object_representations_v<T>)
                   && (bypass_pointer_check
                       || !contains_pointer_representation<T>());
        }

        template<typename Owner, std::meta::info Entity>
        consteval bool entity_uses_forced_object_representation() {
            using declared_type = reflected_entity_type_t<Entity>;
            using value_type = reflected_entity_value_t<Entity>;
            constexpr bool pointer_bypass =
                bypass_pointer_check_annotation_count<Owner>() == 1
                || entity_bypass_pointer_check_annotation_count<Entity>() == 1;
            constexpr bool forced_atomic = field_atomic_annotation_count<Entity>() == 1;
            if constexpr (std::is_reference_v<declared_type>
                          || std::meta::is_bit_field(Entity)
                          || conversion_count<Entity>() != 0
                          || !std::meta::annotations_of_with_type(
                                  Entity, ^^tag::container).empty()
                          || (!forced_atomic
                              && !(pointer_bypass
                                   && contains_pointer_representation<value_type>()))) {
                return false;
            } else if constexpr (forced_atomic) {
                return direct_atomic_representation_supported<value_type>(
                    field_atomic_options<Entity>(), pointer_bypass);
            } else {
                return direct_atomic_representation_supported<value_type>(
                    tag::atomic{}, true);
            }
        }

        template<typename T>
        consteval bool atomic_uses_direct_object_representation_impl() {
            static_assert(atomic_annotation_count<T>() == 1);
            return !has_transient_members<T>()
                   && (atomic_options<T>().allow_padding
                       || std::has_unique_object_representations_v<T>);
        }

        template<typename T>
        consteval bool atomic_uses_segmented_object_representation_impl() {
            if constexpr (!has_transient_members<T>()
                          || !(atomic_options<T>().allow_padding
                               || std::has_unique_object_representations_v<T>)
                          || !std::is_trivially_copyable_v<T>
                          || !transient_layout_supports_segmented_copy<T>()) {
                return false;
            } else {
                return true;
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
            } else if constexpr (auto_seq_annotation_count<T>() != 0) {
                return false;
            } else if constexpr (bypass_pointer_check_annotation_count<T>() > 1) {
                return false;
            } else if constexpr (!(atomic_uses_direct_object_representation_impl<T>()
                                   || atomic_uses_segmented_object_representation_impl<T>())) {
                return false;
            } else if constexpr (!std::is_trivially_copyable_v<T>) {
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
                                      || (Kind == reflected_entity_kind::base
                                       && std::meta::is_virtual(Entity))
                                      || (Kind == reflected_entity_kind::member
                                          && (std::meta::is_bit_field(Entity)
                                              || std::is_reference_v<declared_type>))
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
    }
}
