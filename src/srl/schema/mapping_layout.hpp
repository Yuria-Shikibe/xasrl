#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "model.hpp"

namespace xasrl {
    enum class schema_storage { embedded, schema_only };

    namespace detail {
        inline constexpr std::uint32_t wire_v5_magic = 0x354c5253u;
        inline constexpr std::uint32_t schema_only_marker = 0xffffffffu;
        inline constexpr szt document_header_size = 16;
        inline constexpr szt definition_header_size = 8;
        inline constexpr szt definition_row_size = 16;
        inline constexpr szt definition_reference_size = 4;

        template<typename Buffer>
        consteval void append_le(Buffer &out, szt value, unsigned bytes) {
            for (unsigned i = 0; i != bytes; ++i) {
                out.push_back(static_cast<std::byte>(value & 0xff));
                value >>= 8;
            }
        }

        template<std::size_t N>
        struct mapping_layout {
            struct node_layout {
                std::span<const std::byte> definition;
                std::span<const std::byte> prefix;
                szt embedded_extent{};
                szt local_extent{};
            };
            std::array<node_layout, N> nodes;
            std::span<const std::byte> embedded_header;
            std::span<const std::byte> local_header;
            szt fingerprint;

            consteval schema_extent extent(std::size_t node, bool only) const {
                const auto size = only ? nodes[node].local_extent : nodes[node].embedded_extent;
                return size == std::dynamic_extent ? schema_extent::dynamic() : schema_extent::fixed(size);
            }
        };

        consteval std::vector<std::byte> describe_mapping(const schema_tree &tree, std::size_t index) {
            std::vector<std::byte> bytes;
            const auto node = tree[index];
            if (node.kind() == schema_kind::leaf) { return bytes; }
            const auto children = tree.child_indices(index);
            append_le(bytes, children.size(), 4);
            append_le(bytes, static_cast<unsigned>(node.wire_kind()), 4);
            std::size_t ordinal{};
            for (auto child: children) {
                const auto field = tree[child];
                append_le(bytes, field.field_id().value_or(static_cast<id_type>(ordinal++)), 4);
                append_le(bytes, field.wire_type(), 4);
                append_le(bytes, field.kind() == schema_kind::leaf ? field.encode_extent().value : std::dynamic_extent, 8);
            }
            return bytes;
        }

        consteval void accumulate_occurrences(const schema_tree &tree, std::size_t index,
                                               std::uint8_t count, std::vector<std::uint8_t> &counts) {
            counts[index] = count;
            const auto node = tree[index];
            if (node.kind() == schema_kind::container) {
                const auto n = node.container().element_count;
                if (n == std::dynamic_extent || n == 0) { count = 0; }
                else if (n > 1 && count != 0) { count = 2; }
            }
            for (auto child: tree.child_indices(index)) {
                accumulate_occurrences(tree, child, count, counts);
            }
        }

        template<const schema_tree &Tree>
        consteval auto build_mapping_layout() {
            constexpr auto &tree = Tree;
            using layout = mapping_layout<tree.size()>;
            std::array<typename layout::node_layout, tree.size()> nodes{};
            std::vector<std::uint8_t> occurrences(tree.size());
            accumulate_occurrences(tree, tree.value_index(), 1, occurrences);
            std::vector<std::size_t> group_for(tree.size());
            struct group { std::size_t first; unsigned guaranteed; std::uint32_t reference{}; };
            std::vector<group> groups;
            std::vector<std::byte> directory;
            std::uint32_t count{};
            for (std::size_t i = 1; i != tree.size(); ++i) {
                const auto node = tree[i];
                auto bytes = describe_mapping(tree, i);
                if (bytes.empty()) { continue; }
                nodes[i].definition = std::define_static_array(bytes);
                if (node.inline_mapping()) { continue; }
                auto group_id = groups.size();
                for (std::size_t j = 0; j != groups.size(); ++j) {
                    if (std::ranges::equal(nodes[groups[j].first].definition, bytes)) { group_id = j; break; }
                }
                if (group_id == groups.size()) { groups.push_back({i, 0}); }
                group_for[i] = group_id;
                groups[group_id].guaranteed = std::min(2u, groups[group_id].guaranteed + occurrences[i]);
            }
            for (auto &group: groups) {
                // Both placements have a four-byte selector. Sharing pays for one
                // directory copy of the exact same self-delimiting definition.
                if (group.guaranteed < 2) { continue; }
                group.reference = ++count;
                const auto bytes = nodes[group.first].definition;
                directory.insert(directory.end(), bytes.begin(), bytes.end());
            }
            for (std::size_t i = tree.size(); --i != 0;) {
                const auto node = tree[i];
                if (node.kind() == schema_kind::leaf) {
                    nodes[i].embedded_extent = nodes[i].local_extent = node.encode_extent().value;
                    continue;
                }
                const auto reference = node.inline_mapping() ? 0u : groups[group_for[i]].reference;
                std::vector<std::byte> prefix;
                append_le(prefix, reference, definition_reference_size);
                if (!reference) { prefix.insert(prefix.end(), nodes[i].definition.begin(), nodes[i].definition.end()); }
                nodes[i].prefix = std::define_static_array(prefix);
                const auto measure = [&](bool only) consteval {
                    auto total = schema_extent::fixed(entry_head_wire_size + (only ? 0 : prefix.size()));
                    auto payload = schema_extent::fixed(0);
                    for (auto child: tree.child_indices(i)) {
                        const auto extent = only ? nodes[child].local_extent : nodes[child].embedded_extent;
                        payload += extent == std::dynamic_extent ? schema_extent::dynamic() : schema_extent::fixed(extent);
                    }
                    if (node.kind() == schema_kind::container) {
                        const auto n = node.container().element_count;
                        payload = n == std::dynamic_extent ? schema_extent::dynamic() : payload.scaled_by(n);
                    }
                    return (total + payload).value;
                };
                nodes[i].embedded_extent = measure(false);
                nodes[i].local_extent = measure(true);
            }
            std::vector<std::byte> embedded;
            append_le(embedded, wire_v5_magic, 4);
            append_le(embedded, count, 4);
            append_le(embedded, 0, 8);
            embedded.insert(embedded.end(), directory.begin(), directory.end());
            // Hash the wire tree in decode order, not the order in which
            // reflection happened to discover the C++ declarations.
            szt fingerprint = 14695981039346656037ull;
            const auto hash_bytes = [&](std::span<const std::byte> bytes) {
                for (auto byte: bytes) {
                    fingerprint = (fingerprint ^ std::to_integer<unsigned>(byte)) * 1099511628211ull;
                }
            };
            auto hash_node = [&](auto &self, std::size_t index) consteval -> void {
                const auto node = tree[index];
                std::vector<std::byte> identity;
                append_le(identity, static_cast<unsigned>(node.wire_kind()), 1);
                append_le(identity, node.wire_type(), 4);
                if (node.kind() == schema_kind::leaf) {
                    append_le(identity, node.encode_extent().value, 8);
                    const auto type = node.type();
                    if (!std::meta::is_integral_type(type) && !std::meta::is_floating_point_type(type)) {
                        const auto name = std::meta::display_string_of(type);
                        append_le(identity, name.size(), 8);
                        for (unsigned char c: name) { identity.push_back(static_cast<std::byte>(c)); }
                    } else {
                        append_le(identity, 0, 8);
                    }
                } else if (node.kind() == schema_kind::container) {
                    append_le(identity, node.container().element_count, 8);
                }
                hash_bytes(identity);
                hash_bytes(nodes[index].definition);
                for (auto child: tree.child_indices(index)) { self(self, child); }
            };
            hash_node(hash_node, tree.value_index());
            std::vector<std::byte> local;
            append_le(local, wire_v5_magic, 4);
            append_le(local, schema_only_marker, 4);
            append_le(local, fingerprint, 8);
            const auto root = tree.value_index();
            const auto root_extent = [&](bool only) consteval {
                const auto value = only ? nodes[root].local_extent : nodes[root].embedded_extent;
                const auto extra = (only ? local.size() : embedded.size())
                    + (tree[root].kind() == schema_kind::leaf ? entry_head_wire_size : 0);
                return value == std::dynamic_extent ? value : (schema_extent::fixed(value) + schema_extent::fixed(extra)).value;
            };
            nodes[0].embedded_extent = root_extent(false);
            nodes[0].local_extent = root_extent(true);
            return layout{nodes, std::define_static_array(embedded), std::define_static_array(local), fingerprint};
        }

        template<const schema_tree &Tree>
        inline constexpr auto mapping_layout_for = build_mapping_layout<Tree>();
    }
}
