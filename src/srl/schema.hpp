#pragma once

#include "enum.hpp"

#include <concepts>
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>
#include <utility>

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

    struct schema {
        const schema_tree tree;

        consteval explicit schema(std::meta::info type)
            : schema(type, detail::annotation_count(std::meta::remove_cvref(type), ^^tag::auto_stream) != 0) {}

        template<typename T>
        consteval explicit schema(std::in_place_type_t<T>)
            : schema(^^T, reflected_stream<T>) {}

        consteval schema(std::meta::info type, bool stream)
            : tree(detail::build_schema(type, stream)) {}

        [[nodiscard]] consteval schema_node root_node() const noexcept {
            return tree.root_node();
        }

        [[nodiscard]] consteval schema_node value_node() const noexcept {
            return tree.value_node();
        }
    };

    namespace detail {
        template<typename T>
        inline constexpr schema schema_for{^^T};

        template<typename T>
        inline constexpr schema_extent root_encode_extent = schema_for<T>.root_node().encode_extent();
    }

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
        const auto node = tree.value_node();
        if (node.kind() == schema_kind::record || node.kind() == schema_kind::stream) {
            return tree.child_indices(tree.value_index()).size();
        } else {
            return 0;
        }
    }

}
