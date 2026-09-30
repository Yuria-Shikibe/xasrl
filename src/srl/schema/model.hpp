#pragma once

#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>

#include "../context.hpp"
#include "../wire.hpp"

namespace srl {
    enum class schema_kind : std::uint8_t { root, atomic, record, stream, container };

    enum class schema_role : std::uint8_t { value, member, base, element, key, mapped };

    enum class atomic_representation : std::uint8_t { codec, direct, segmented, forced };

    enum class schema_duplicates : std::uint8_t { inherit, deduplicate, reject };

    struct schema_node {
        schema_kind kind = schema_kind::root;
        schema_role role = schema_role::value;
        std::meta::info type{};
        std::meta::info memory_type{};
        std::meta::info entity{};
        id_type id{};
        type_hash_type type_hash{};
        std::size_t children_begin{};
        std::size_t children_count{};
        std::size_t encode_extent = std::dynamic_extent;
        std::size_t decode_extent = std::dynamic_extent;
        std::size_t element_count = std::dynamic_extent;
        std::size_t header_size{};
        std::uint8_t mapping_group_size{};
        entry_kind wire_kind = entry_kind::atomic;
        mapping_kind mapping = mapping_kind::none;
        atomic_representation representation = atomic_representation::codec;
        conversion_tag conversion{};
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
