#pragma once

#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>

#include "../context.hpp"
#include "../wire.hpp"
#include "../detail/object_layout.hpp"

namespace srl {
    enum class schema_kind : std::uint8_t { root, leaf, record, stream, container };

    enum class schema_role : std::uint8_t { value, member, base, element, key, mapped };

    enum class leaf_representation : std::uint8_t { codec, direct, segmented, forced };

    enum class schema_duplicates : std::uint8_t { inherit, deduplicate, reject };

    enum class schema_error : std::uint8_t {
        none, invalid_schema, unsupported_type, conflicting_annotations, duplicate_field_id,
        field_id_overflow, recursive_type, invalid_conversion, invalid_object_representation,
        extent_overflow, mapping_limit, missing_default_constructor, unsupported_entity,
    };

    struct schema_issue {
        schema_error reason{};
        std::meta::info entity{};
    };

    struct schema_node {
        schema_kind kind = schema_kind::root;
        schema_role role = schema_role::value;
        std::meta::info type{};
        std::meta::info memory_type{};
        std::meta::info entity{};
        id_type id{};
        bool has_field_id{};
        type_hash_type type_hash{};
        type_hash_type wire_type{};
        std::size_t children_begin{};
        std::size_t children_count{};
        szt encode_extent = std::dynamic_extent;
        szt decode_extent = std::dynamic_extent;
        std::size_t element_count = std::dynamic_extent;
        szt header_size{};
        std::uint8_t mapping_group_size{};
        entry_kind wire_kind = entry_kind::leaf;
        leaf_representation representation = leaf_representation::codec;
        detail::object_layout object{};
        conversion_tag conversion{};
        std::meta::info fallback_type{};
        schema_duplicates duplicates = schema_duplicates::inherit;

        [[nodiscard]] consteval bool converted() const noexcept {
            return conversion.target_type != std::meta::info{};
        }
    };

    struct schema_tree {
        bool valid{};
        std::span<const schema_node> nodes;
        std::span<const std::size_t> child_indices;
        std::size_t root{};
        schema_issue issue{};

        [[nodiscard]] consteval const schema_node &node(std::size_t index) const noexcept {
            return nodes[index];
        }

        [[nodiscard]] consteval std::span<const std::size_t> children(std::size_t index) const noexcept {
            const auto &value = node(index);
            return child_indices.subspan(value.children_begin, value.children_count);
        }

        [[nodiscard]] consteval std::size_t child(std::size_t index, std::size_t ordinal) const noexcept {
            return children(index)[ordinal];
        }
    };
}
