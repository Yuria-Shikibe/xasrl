#pragma once

#include "enum.hpp"

#include <concepts>
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>

#include "schema/build.hpp"

namespace srl {
    consteval bool has_serialized_fields(std::meta::info type) {
        return detail::schema_has_fields(std::meta::remove_cvref(type));
    }

    template<typename T>
    concept reflected_record = has_serialized_fields(^^T)
        && detail::annotation_count(^^codec_value_type<T>, ^^tag::auto_stream) == 0
        && detail::annotation_count(^^codec_value_type<T>, ^^tag::leaf) == 0;

    template<typename T>
    concept reflected_stream =
        detail::annotation_count(^^codec_value_type<T>, ^^tag::auto_stream) != 0;

    template<typename T>
    concept container_serializable = detail::adapted_container<codec_value_type<T>>;

    template<typename T>
    concept serializable = leaf_serializable<T> || reflected_record<T>
                           || reflected_stream<T> || container_serializable<T>;

    template<typename T, bool Stream = reflected_stream<T>>
    struct schema {
        static constexpr schema_tree tree = detail::build_schema(^^T, Stream);
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

    [[nodiscard]] consteval schema_issue inspect_schema(std::meta::info type) {
        try {
            (void)detail::build_schema(type);
            return {};
        } catch (const detail::schema_failure &failure) {
            return failure.issue();
        }
    }

    consteval bool schema_valid(std::meta::info type) {
        return inspect_schema(type).reason == schema_error::none;
    }

    [[nodiscard]] consteval std::string_view schema_diagnostic(std::meta::info type) {
        const auto result = format_schema_issue(inspect_schema(type));
        return {std::define_static_string(result), result.size()};
    }

    consteval std::size_t serialized_field_count(std::meta::info type) {
        const auto tree = detail::build_schema(type);
        const auto &node = tree.node(tree.child(tree.root, 0));
        if (node.kind == schema_kind::record || node.kind == schema_kind::stream) {
            return node.children_count;
        } else {
            return 0;
        }
    }

}
