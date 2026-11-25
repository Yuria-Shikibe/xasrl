#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <optional>
#include <span>
#include <utility>

#include "extent.hpp"
#include "../types.hpp"
#include "../wire_format.hpp"
#include "../detail/object_layout_model.hpp"

namespace xasrl {
    enum class schema_kind : std::uint8_t { root, leaf, record, stream, container };

    enum class leaf_representation : std::uint8_t { codec, direct, segmented, forced };

    enum class schema_duplicates : std::uint8_t { inherit, deduplicate, reject };

    enum class schema_binding_kind : std::uint8_t { none, record_field, stream_field, slot };
    enum class schema_slot_role : std::uint8_t { element, key, mapped };
    enum class schema_conversion_kind : std::uint8_t { none, cast, handler };
    enum class schema_container_shape : std::uint8_t { sequence, map };
    enum class leaf_strategy : std::uint8_t { custom, scalar_checked, scalar_unchecked, object, forced };

    struct schema_field_binding {
        std::meta::info entity;
        schema_conversion_kind conversion = schema_conversion_kind::none;
        const void *handler_id{};

        [[nodiscard]] consteval std::meta::info memory_type() const {
            return std::meta::remove_cvref(std::meta::type_of(entity));
        }

        [[nodiscard]] consteval bool is_base() const { return std::meta::is_base(entity); }
    };

    struct schema_record_field {
        schema_field_binding field;
        id_type id;
        std::meta::info fallback{};
        const id_type *alias_data{};
        std::size_t alias_count{};

        [[nodiscard]] consteval std::span<const id_type> accepted_ids() const noexcept {
            return {alias_data, alias_count};
        }

        [[nodiscard]] consteval std::optional<std::meta::info> fallback_type() const {
            if (fallback != std::meta::info{}) { return fallback; }
            return std::nullopt;
        }
    };

    struct schema_slot {
        schema_slot_role role;
        std::meta::info declared_type;
    };

    struct schema_leaf {
        leaf_strategy strategy;
        leaf_representation representation;

        detail::object_layout object{};

        [[nodiscard]] consteval detail::object_layout layout() const noexcept { return object; }

        [[nodiscard]] consteval bool scalar() const noexcept {
            return strategy == leaf_strategy::scalar_checked || strategy == leaf_strategy::scalar_unchecked;
        }
    };

    struct schema_container {
        std::size_t slots_begin;
        std::size_t element_count;
        schema_container_shape shape;
        schema_duplicates duplicates;

        [[nodiscard]] consteval std::uint8_t slot_count() const noexcept {
            return shape == schema_container_shape::sequence ? 1 : 2;
        }

        [[nodiscard]] consteval entry_kind wire_kind() const noexcept {
            return shape == schema_container_shape::sequence ? entry_kind::sequence : entry_kind::map;
        }
    };

    class schema_tree;

    namespace detail {
        // define_static_array requires structural elements. Only the builder writes
        // these tagged unions; public node views refer to the frozen array.
        struct schema_binding {
            schema_binding_kind kind = schema_binding_kind::none;
            union {
                schema_record_field record;
                schema_field_binding stream;
                schema_slot slot;
            };

            [[nodiscard]] consteval bool is_field() const noexcept {
                return kind == schema_binding_kind::record_field || kind == schema_binding_kind::stream_field;
            }

            [[nodiscard]] consteval const schema_field_binding &field() const noexcept {
                return kind == schema_binding_kind::record_field ? record.field : stream;
            }
        };

        struct schema_edges {
            std::size_t begin;
            std::size_t count;
        };

        struct schema_root {
            std::size_t child_begin;
            entry_kind wire_kind;
        };

        struct schema_stream {
            schema_edges edges;
            schema_extent decode_extent;
        };

        union schema_shape {
            schema_root root;
            schema_leaf leaf;
            schema_edges record;
            schema_stream stream;
            schema_container container;
        };

        struct schema_node_storage {
            std::meta::info type;
            type_hash_type wire_type;
            schema_extent encode_extent;
            schema_binding binding;
            schema_kind kind;
            schema_shape shape;
            bool inline_mapping{};

            [[nodiscard]] consteval schema_extent decode_extent() const noexcept {
                if (kind == schema_kind::leaf) { return encode_extent; }
                if (kind == schema_kind::stream) { return shape.stream.decode_extent; }
                return schema_extent::dynamic();
            }

            [[nodiscard]] consteval entry_kind wire_kind() const noexcept {
                switch (kind) {
                    case schema_kind::root: return shape.root.wire_kind;
                    case schema_kind::leaf: return entry_kind::leaf;
                    case schema_kind::record: return entry_kind::record;
                    case schema_kind::stream: return entry_kind::stream;
                    case schema_kind::container: return shape.container.wire_kind();
                }
                std::unreachable();
            }
        };

        consteval schema_tree build_schema(std::meta::info type, bool stream);
    }

    class schema_node {
        const detail::schema_node_storage *storage_;

        explicit consteval schema_node(const detail::schema_node_storage &storage) noexcept
            : storage_(&storage) {}

        friend class schema_tree;

    public:
        [[nodiscard]] consteval bool inline_mapping() const noexcept { return storage_->inline_mapping; }
        [[nodiscard]] consteval schema_kind kind() const noexcept { return storage_->kind; }
        [[nodiscard]] consteval std::meta::info type() const noexcept { return storage_->type; }
        [[nodiscard]] consteval type_hash_type wire_type() const noexcept { return storage_->wire_type; }
        [[nodiscard]] consteval schema_extent encode_extent() const noexcept { return storage_->encode_extent; }

        [[nodiscard]] consteval schema_extent decode_extent() const noexcept { return storage_->decode_extent(); }
        [[nodiscard]] consteval entry_kind wire_kind() const noexcept { return storage_->wire_kind(); }

        // Category-specific access requires the corresponding kind or binding_kind.
        [[nodiscard]] consteval const schema_leaf &leaf() const noexcept { return storage_->shape.leaf; }
        [[nodiscard]] consteval const schema_container &container() const noexcept { return storage_->shape.container; }

        [[nodiscard]] consteval schema_binding_kind binding_kind() const noexcept { return storage_->binding.kind; }
        [[nodiscard]] consteval const schema_record_field &record_field() const noexcept { return storage_->binding.record; }
        [[nodiscard]] consteval const schema_field_binding &stream_field() const noexcept { return storage_->binding.stream; }
        [[nodiscard]] consteval const schema_slot &slot() const noexcept { return storage_->binding.slot; }

        [[nodiscard]] consteval bool is_field() const noexcept {
            return storage_->binding.is_field();
        }

        [[nodiscard]] consteval const schema_field_binding &field() const noexcept { return storage_->binding.field(); }

        [[nodiscard]] consteval std::optional<id_type> field_id() const noexcept {
            if (binding_kind() == schema_binding_kind::record_field) { return record_field().id; }
            return std::nullopt;
        }

        [[nodiscard]] consteval bool converted() const noexcept {
            return is_field() && field().conversion != schema_conversion_kind::none;
        }
    };

    class schema_tree {
        std::span<const detail::schema_node_storage> nodes_;
        std::span<const std::size_t> child_indices_;

        consteval schema_tree(std::span<const detail::schema_node_storage> nodes,
                              std::span<const std::size_t> child_indices) noexcept
            : nodes_(nodes), child_indices_(child_indices) {}

        friend consteval schema_tree detail::build_schema(std::meta::info, bool);

    public:
        static constexpr std::size_t root_index = 0;
        [[nodiscard]] consteval std::size_t size() const noexcept { return nodes_.size(); }

        template<std::integral... Ordinals>
        [[nodiscard]] consteval schema_node operator[](
            std::size_t index, Ordinals... ordinals) const noexcept {
            ((index = child_indices(index)[static_cast<std::size_t>(ordinals)]), ...);
            return schema_node{nodes_[index]};
        }

        [[nodiscard]] consteval std::span<const std::size_t> child_indices(std::size_t index) const noexcept {
            const auto &row = nodes_[index];
            switch (row.kind) {
                case schema_kind::root: return child_indices_.subspan(row.shape.root.child_begin, 1);
                case schema_kind::leaf: return {};
                case schema_kind::record: return child_indices_.subspan(row.shape.record.begin, row.shape.record.count);
                case schema_kind::stream: return child_indices_.subspan(row.shape.stream.edges.begin, row.shape.stream.edges.count);
                case schema_kind::container:
                    return child_indices_.subspan(row.shape.container.slots_begin, row.shape.container.slot_count());
            }
            std::unreachable();
        }

        [[nodiscard]] consteval std::size_t value_index() const noexcept {
            return child_indices(root_index)[0];
        }

        [[nodiscard]] consteval schema_node root_node() const noexcept {
            return (*this)[root_index];
        }

        [[nodiscard]] consteval schema_node value_node() const noexcept {
            return (*this)[value_index()];
        }
    };
}
