#pragma once

#include <concepts>
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>

#include "schema/build.hpp"

namespace srl {
    template<typename T>
    consteval bool has_serialized_fields() {
        return detail::schema_has_fields(^^codec_value_type<T>);
    }

    template<typename T>
    concept reflected_record = has_serialized_fields<T>()
        && detail::annotation_count(^^codec_value_type<T>, ^^tag::auto_stream) == 0
        && detail::annotation_count(^^codec_value_type<T>, ^^tag::atomic) == 0;

    template<typename T>
    concept reflected_stream =
        detail::annotation_count(^^codec_value_type<T>, ^^tag::auto_stream) != 0;

    template<typename T>
    concept container_serializable = detail::adapted_container<codec_value_type<T>>;

    template<typename T>
    concept serializable = atomic_serializable<T> || reflected_record<T>
                           || reflected_stream<T> || container_serializable<T>;

    template<typename T, bool Stream = reflected_stream<T>>
    struct schema {
        static constexpr schema_tree tree = detail::build_schema(^^T, Stream);
        static constexpr bool valid = tree.valid;
        static constexpr std::size_t root = tree.root;
        static constexpr std::size_t value = tree.child(root, 0);
        static constexpr auto nodes = tree.nodes;
        static constexpr auto child_indices = tree.child_indices;

        static consteval const schema_node &node(std::size_t index) noexcept {
            return tree.node(index);
        }

        static consteval std::size_t child(std::size_t index, std::size_t ordinal) noexcept {
            return tree.child(index, ordinal);
        }

        static consteval auto children(std::size_t index) noexcept {
            return tree.children(index);
        }
    };

    template<typename T>
    consteval bool schema_valid() {
        return schema<T>::valid;
    }

    template<typename T>
    [[nodiscard]] consteval std::string_view schema_diagnostic() {
        constexpr auto issue = schema<T>::tree.issue;
        std::string result(enum_name(issue.reason));
        if constexpr (issue.entity != std::meta::info{}) {
            result.append(": ");
            result.append(std::meta::display_string_of(issue.entity));
        }
        return {std::define_static_string(result), result.size()};
    }

    template<typename T>
    concept schemaed = schema<T>::valid;

    template<typename T>
    consteval std::size_t serialized_field_count() {
        static_assert(schema<T>::valid, schema_diagnostic<T>());
        constexpr auto &node = schema<T>::node(schema<T>::value);
        if constexpr (node.kind == schema_kind::record || node.kind == schema_kind::stream) {
            return node.children_count;
        } else {
            return 0;
        }
    }

}
