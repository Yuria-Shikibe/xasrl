#pragma once

#include <cstddef>
#include <iterator>
#include <meta>
#include <string>
#include <string_view>

#include "schema.hpp"

namespace srl::detail {
    consteval void append_schema_indent(std::string &out, std::size_t depth) {
        out.append(depth * 2, ' ');
    }

    consteval void append_schema_unsigned(std::string &out, std::size_t value) {
        char digits[sizeof(value) * 3]{};
        auto cursor = std::end(digits);
        do {
            *--cursor = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        out.append(cursor, std::end(digits));
    }

    consteval std::string_view schema_type_name(std::meta::info type) {
        return std::meta::display_string_of(std::meta::is_same_type(type, ^^std::string) ? ^^std::string : type);
    }

    consteval void append_schema_entity_name(std::string &out, std::meta::info entity) {
        if (std::meta::is_base(entity)) {
            out.append("base<");
            out.append(schema_type_name(std::meta::type_of(entity)));
            out.push_back('>');
        } else {
            out.append(std::meta::identifier_of(entity));
        }
    }

    // Leaf object members explain the representation; they are not wire children.
    consteval void append_leaf_schema(std::string &out, const schema_node &node,
                                       std::meta::info display_type, std::size_t depth, bool leaf_root) {
        const auto type = node.type;
        const bool object = node.representation != leaf_representation::codec;
        const bool forced = node.representation == leaf_representation::forced;
        if (forced) {
            out.append("object-leaf ");
        } else if (std::meta::is_arithmetic_type(type) || std::meta::is_enum_type(type)) {
            out.append("primitive-leaf ");
        } else {
            out.append(object ? "object-leaf " : "codec-leaf ");
        }
        out.append(schema_type_name(display_type));
        out.append(" (size: ");
        append_schema_unsigned(out, node.encode_extent);
        out.append(node.encode_extent == 1 ? " byte" : " bytes");
        if (object) {
            out.append(", representation: ");
            out.append(forced ? "forced-direct" : node.representation == leaf_representation::direct ? "direct" : "segmented");
        }
        out.push_back(')');
        if (!object || (!std::meta::is_class_type(type) && !std::meta::is_union_type(type))) {
            return;
        }
        out.append(" {\n");
        for (const auto field: node.object.fields()) {
            append_schema_indent(out, depth + 1);
            out.append("[wire +");
            append_schema_unsigned(out, field.wire_offset);
            out.append("] ");
            append_schema_entity_name(out, field.entity);
            out.append(": ");
            out.append(schema_type_name(field.type));
            out.append(" (size: ");
            append_schema_unsigned(out, field.size);
            out.append(", object +");
            append_schema_unsigned(out, field.object_offset);
            out.push_back(')');
            out.push_back('\n');
        }
        append_schema_indent(out, depth);
        out.push_back('}');
    }

    consteval void append_schema_node(std::string &out, const schema_tree &tree, std::size_t index,
                                     std::meta::info display_type, std::size_t depth, bool leaf_root = true) {
        const auto &node = tree.node(index);
        if (node.kind == schema_kind::leaf) {
            append_leaf_schema(out, node, display_type, depth, leaf_root);
            return;
        }
        const bool container = node.kind == schema_kind::container;
        out.append(container ? "container " : node.kind == schema_kind::stream ? "stream " : "record ");
        out.append(schema_type_name(display_type));
        if (container) {
            out.append(" (count: ");
            if (node.element_count == std::dynamic_extent) {
                out.append("dynamic");
            } else {
                append_schema_unsigned(out, node.element_count);
            }
            out.push_back(')');
        }
        out.append(" {\n");
        std::size_t position{};
        for (auto child_index: tree.children(index)) {
            const auto &child = tree.node(child_index);
            append_schema_indent(out, depth + 1);
            if (container) {
                out.append(child.role == schema_role::element ? "element: " : child.role == schema_role::key ? "key: " : "value: ");
                append_schema_node(out, tree, child_index, child.type, depth + 1);
            } else {
                out.push_back('[');
                if (node.kind == schema_kind::stream) {
                    out.append("position ");
                    append_schema_unsigned(out, position);
                } else {
                    append_schema_unsigned(out, child.id);
                }
                ++position;
                out.append("] ");
                if (child.kind == schema_kind::leaf) {
                    out.append(child.role == schema_role::base ? "[leaf-root base] " : "[leaf-root memb] ");
                }
                append_schema_entity_name(out, child.entity);
                out.append(": ");
                append_schema_node(out, tree, child_index,
                    child.converted() ? child.conversion.target_type : std::meta::type_of(child.entity), depth + 1, false);
                if (child.converted() && !std::meta::is_same_type(child.memory_type, child.type)) {
                    out.append(" <- ");
                    out.append(schema_type_name(std::meta::type_of(child.entity)));
                }
                if (child.duplicates == schema_duplicates::reject) {
                    out.append(" [duplicates: reject]");
                }
                if (child.has_field_id) {
                    out.append(child.fallback_type == std::meta::info{} ? " [required]" : " [fallback]");
                }
            }
            out.push_back('\n');
        }
        append_schema_indent(out, depth);
        out.push_back('}');
    }

    consteval std::string_view make_schema_string(const schema_tree &tree, std::meta::info display_type) {
        std::string result;
        append_schema_node(result, tree, tree.child(tree.root, 0), display_type, 0);
        return {std::define_static_string(result), result.size()};
    }
}

namespace srl {
    [[nodiscard]] consteval std::string_view schema_string(std::meta::info type) {
        return detail::make_schema_string(detail::build_schema(type), type);
    }
}
