#pragma once

#include <cstddef>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>

#include "schema.hpp"

namespace srl {
    namespace detail {
        consteval void append_schema_indent(std::string &out, std::size_t depth) {
            out.append(depth * 2, ' ');
        }

        consteval void append_schema_unsigned(std::string &out, std::size_t value) {
            //TODO use std::to_chars
            char digits[sizeof(value) * 3]{};
            auto cursor = std::end(digits);
            do {
                *--cursor = static_cast<char>('0' + value % 10);
                value /= 10;
            } while (value != 0);
            out.append(cursor, std::end(digits));
        }

        template<std::meta::info Type>
        consteval std::string_view schema_type_name() {
            if constexpr (std::meta::is_same_type(Type, ^^std::string)){
                return std::meta::display_string_of(^^std::string);
            } else {
                return std::meta::display_string_of(Type);
            }
        }

        template<typename T, std::meta::info DisplayType = ^^T>
        consteval void append_schema_node(
            std::string &out,
            std::size_t depth,
            bool atomic_root = true);

        template<typename T, std::meta::info DisplayType>
        consteval void append_atomic_schema(
            std::string &out,
            std::size_t depth,
            bool atomic_root) {
            using value_type = codec_value_type<T>;
            if constexpr (primitive<value_type>) {
                out.append("primitive-atom ");
            } else if constexpr (auto_atomic_supported<value_type>()) {
                out.append("object-atom ");
            } else {
                out.append("codec-atom ");
            }
            out.append(schema_type_name<DisplayType>());
            out.append(" (size: ");
            constexpr auto fixed_size = atomic_codec<value_type>::fixed_size;
            detail::append_schema_unsigned(out, fixed_size);
            out.append(fixed_size == 1 ? " byte" : " bytes");
            if constexpr (auto_atomic_supported<value_type>()) {
                if constexpr (atomic_codec<value_type>::uses_direct_object_representation) {
                    out.append(", representation: direct)");
                } else {
                    static_assert(atomic_codec<value_type>::uses_segmented_object_representation);
                    out.append(", representation: segmented)");
                }
                out.append(" {\n");
                detail::for_each_atomic_entity<value_type>(
                    [&]<std::meta::info Entity, reflected_entity_kind Kind> {
                        using entity_type = reflected_entity_value_t<Entity>;
                        append_schema_indent(out, depth + 1);
                        out.append(atomic_root ? "[atom-root " : "[atom ");
                        out.append(Kind == reflected_entity_kind::member ? "memb] " : "base] ");
                        if constexpr (Kind == reflected_entity_kind::member) {
                            out.append(std::meta::identifier_of(Entity));
                        } else {
                            out.append("base<");
                            out.append(std::meta::display_string_of(std::meta::type_of(Entity)));
                            out.push_back('>');
                        }
                        out.append(": ");
                        if constexpr (atomic_serializable<entity_type>) {
                            append_atomic_schema<entity_type, std::meta::type_of(Entity)>(
                                out, depth + 1, false);
                        } else {
                            out.append(std::meta::display_string_of(std::meta::type_of(Entity)));
                        }
                        out.push_back('\n');
                    });
                append_schema_indent(out, depth);
                out.push_back('}');
            } else {
                out.push_back(')');
            }
        }

        template<typename T, std::meta::info DisplayType>
        consteval void append_container_schema(std::string &out, std::size_t depth) {
            using value_type = codec_value_type<T>;
            using traits = container_traits<value_type>;
            out.append("container ");
            out.append(schema_type_name<DisplayType>());
            out.append(" (count: ");
            if constexpr (traits::fixed_size == std::dynamic_extent) {
                out.append("dynamic");
            } else {
                detail::append_schema_unsigned(out, traits::fixed_size);
            }
            out.append(") {\n");
            if constexpr (traits::mapping_group_size == 1) {
                append_schema_indent(out, depth + 1);
                out.append("element: ");
                append_schema_node<typename traits::element_type>(out, depth + 1);
                out.push_back('\n');
            } else {
                append_schema_indent(out, depth + 1);
                out.append("key: ");
                append_schema_node<typename traits::key_type>(out, depth + 1);
                out.push_back('\n');
                append_schema_indent(out, depth + 1);
                out.append("value: ");
                append_schema_node<typename traits::mapped_type>(out, depth + 1);
                out.push_back('\n');
            }
            append_schema_indent(out, depth);
            out.push_back('}');
        }

        template<typename Owner, std::meta::info Entity>
        consteval bool field_uses_atomic_mode() {
            using storage_type = field_storage_t<Entity>;
            return entity_uses_forced_object_representation<Owner, Entity>()
                   || atomic_serializable<storage_type>;
        }

        template<typename T, std::meta::info DisplayType>
        consteval void append_forced_atomic_schema(std::string &out, std::size_t depth) {
            out.append("object-atomic ");
            out.append(schema_type_name<DisplayType>());
            out.append(" (size: ");
            append_schema_unsigned(out, sizeof(T));
            out.append(sizeof(T) == 1
                           ? " byte, representation: forced-direct)"
                           : " bytes, representation: forced-direct)");
            if constexpr (std::is_class_v<T> || std::is_union_v<T>) {
                out.append(" {\n");
                detail::for_each_reflected_entity<T>(
                    [&]<std::meta::info Child, reflected_entity_kind Kind> {
                        append_schema_indent(out, depth + 1);
                        //TODO use enum to string instead
                        out.append(Kind == reflected_entity_kind::member
                                       ? "[atomic member] "
                                       : "[atomic base] ");
                        if constexpr (Kind == reflected_entity_kind::member) {
                            out.append(std::meta::identifier_of(Child));
                        } else {
                            out.append("base<");
                            out.append(std::meta::display_string_of(std::meta::type_of(Child)));
                            out.push_back('>');
                        }
                        out.append(": ");
                        out.append(std::meta::display_string_of(std::meta::type_of(Child)));
                        out.push_back('\n');
                    });
                append_schema_indent(out, depth);
                out.push_back('}');
            }
        }

        template<typename Owner, std::meta::info Entity>
        consteval void append_field_schema(std::string &out, std::size_t depth) {
            using memory_type = reflected_entity_value_t<Entity>;
            using storage_type = field_storage_t<Entity>;
            if constexpr (entity_uses_forced_object_representation<Owner, Entity>()) {
                append_forced_atomic_schema<memory_type, std::meta::type_of(Entity)>(out, depth);
            } else {
                static constexpr auto storage_info = [] consteval {
                    if constexpr (conversion_count<Entity>() == 1) {
                        return conversion_options<Entity>().target_type;
                    } else {
                        return std::meta::type_of(Entity);
                    }
                }();
                append_schema_node<storage_type, storage_info>(out, depth, false);
                if constexpr (!std::same_as<memory_type, storage_type>) {
                    out.append(" <- ");
                    out.append(schema_type_name<std::meta::type_of(Entity)>());
                }
            }

            static constexpr auto container_annotations = std::define_static_array(
                std::meta::annotations_of_with_type(Entity, ^^tag::container));
            if constexpr (container_annotations.size() == 1) {
                constexpr auto options = std::meta::extract<tag::container>(
                    container_annotations.front());
                if constexpr (options.duplicates == duplicate_policy::reject) {
                    out.append(" [duplicates: reject]");
                }
            }
        }

        template<typename T, std::meta::info DisplayType>
        consteval void append_record_schema(std::string &out, std::size_t depth) {
            using value_type = codec_value_type<T>;
            out.append("record ");
            out.append(schema_type_name<DisplayType>());
            out.append(" {\n");
            detail::for_each_serialized_entity<value_type>(
                [&]<std::meta::info Entity, reflected_entity_kind Kind>(tag::field field) {
                    append_schema_indent(out, depth + 1);
                    out.push_back('[');
                    append_schema_unsigned(out, field.id);
                    out.append("] ");
                    if constexpr (field_uses_atomic_mode<value_type, Entity>()) {
                        out.append(Kind == reflected_entity_kind::member
                                       ? "[atom-root memb] "
                                       : "[atom-root base] ");
                    }
                    if constexpr (Kind == reflected_entity_kind::member) {
                        out.append(std::meta::identifier_of(Entity));
                    } else {
                        out.append("base<");
                        out.append(std::meta::display_string_of(std::meta::type_of(Entity)));
                        out.push_back('>');
                    }
                    out.append(": ");
                    append_field_schema<value_type, Entity>(out, depth + 1);
                    out.push_back('\n');
                });
            append_schema_indent(out, depth);
            out.push_back('}');
        }

        template<typename T, std::meta::info DisplayType>
        consteval void append_schema_node(
            std::string &out,
            std::size_t depth,
            bool atomic_root) {
            using value_type = codec_value_type<T>;
            if constexpr (atomic_serializable<value_type>) {
                append_atomic_schema<T, DisplayType>(out, depth, atomic_root);
            } else if constexpr (container_serializable<value_type>) {
                append_container_schema<T, DisplayType>(out, depth);
            } else {
                static_assert(reflected_record<value_type>);
                append_record_schema<T, DisplayType>(out, depth);
            }
        }

        struct static_schema_string {
            const char *data;
            std::size_t size;
        };

        template<std::meta::info Type>
        consteval static_schema_string make_schema_string() {
            static_assert(std::meta::is_type(Type), "schema visualization requires a type reflection");
            using T = typename[:Type:];
            static_assert(schema_valid<T>(), "cannot visualize an invalid serialization schema");
            std::string result;
            append_schema_node<T, Type>(result, 0);
            return {
                .data = std::define_static_string(result),
                .size = result.size(),
            };
        }
    }

    template<std::meta::info Type>
        requires (std::meta::is_type(Type))
    [[nodiscard]] consteval std::string_view schema_string() noexcept {
        static constexpr auto storage = detail::make_schema_string<Type>();
        return {storage.data, storage.size};
    }

    template<typename T>
    [[nodiscard]] consteval std::string_view schema_string() noexcept {
        return schema_string<^^T>();
    }
}
