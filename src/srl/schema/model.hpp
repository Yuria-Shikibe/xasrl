#pragma once

#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>

#include "diagnostic.hpp"
#include "extent.hpp"
#include "../conversion.hpp"
#include "../types.hpp"
#include "../detail/object_layout_model.hpp"

namespace srl {
    enum class schema_kind : std::uint8_t { root, leaf, record, stream, container };

    enum class schema_role : std::uint8_t { value, member, base, element, key, mapped };

    enum class leaf_representation : std::uint8_t { codec, direct, segmented, forced };

    enum class schema_duplicates : std::uint8_t { inherit, deduplicate, reject };

    struct schema_node {
        schema_kind kind = schema_kind::root;
        schema_role role = schema_role::value;
        std::meta::info type{};
        std::meta::info memory_type{};
        std::meta::info entity{};
        id_type id{};
        bool has_field_id{};
        type_hash_type wire_type{};
        std::size_t children_begin{};
        std::size_t children_count{};
        schema_extent encode_extent = schema_extent::dynamic();
        schema_extent decode_extent = schema_extent::dynamic();
        std::size_t element_count = std::dynamic_extent;
        schema_extent header_size = schema_extent::fixed(0);
        std::uint8_t mapping_group_size{};
        entry_kind wire_kind = entry_kind::leaf;
        leaf_representation representation = leaf_representation::codec;
        detail::object_layout object{};
        conversion_tag conversion{};
        std::meta::info fallback_type{};
        schema_duplicates duplicates = schema_duplicates::inherit;
        detail::validation_mode validation = detail::validation_mode::checked;
        bool builtin_scalar_codec{};
        bool builtin_object_codec{};

        [[nodiscard]] consteval bool converted() const noexcept {
            return conversion.target_type != std::meta::info{};
        }
    };

    struct schema_tree {
        std::span<const schema_node> nodes{};
        std::span<const std::size_t> child_indices{};
        std::size_t root{};

        //TODO use operator[] instead of node()
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
