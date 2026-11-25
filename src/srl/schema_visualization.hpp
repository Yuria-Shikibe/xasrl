#pragma once

#include <array>
#include <cstddef>
#include <format>
#include <meta>
#include <numeric>
#include <span>
#include <string>
#include <string_view>

#include "number_to_string.hpp"
#include "schema.hpp"

namespace xasrl {
    struct schema_format_options {
        bool detailed = false;
        bool memory_layout = false;
        std::size_t memory_layout_exponent = 3;
    };

    struct schema_print_alias_designation {
        std::meta::info type;
    };

    inline constexpr std::size_t maximum_memory_layout_exponent = 5;

    template<std::meta::info Type>
    struct schema_view {};
}

namespace xasrl::detail {
    // A fixed buffer keeps consteval operation counts far lower than std::string,
    // so multiple full renders fit within the compiler's evaluation limit.
    struct schema_buffer {
        static constexpr std::size_t capacity = 1 << 14;
        char data[capacity]{};
        std::size_t size{};

        consteval void push(char value) {
            data[size++] = value;
        }

        consteval void append(std::string_view text) {
            for (const char value: text) {
                data[size++] = value;
            }
        }

        consteval void append(std::size_t count, char value) {
            for (std::size_t i = 0; i != count; ++i) {
                data[size++] = value;
            }
        }

        [[nodiscard]] consteval std::string_view view() const {
            return {data, size};
        }
    };

    consteval void append_schema_indent(schema_buffer &out, std::size_t depth) {
        out.append(depth * 2, ' ');
    }

    consteval std::size_t schema_unsigned_width(szt value) {
        return unsigned_digit_count<10>(value);
    }

    consteval void append_schema_pad(schema_buffer &out, std::size_t width, std::size_t target) {
        if (width < target) {
            out.append(target - width, ' ');
        }
    }

    consteval void append_schema_unsigned(schema_buffer &out, szt value) {
        const auto text = number_to_string(value);
        out.append(text.view());
    }

    consteval std::string_view schema_type_name(std::meta::info type) {
        if (!std::meta::is_type_alias(type) && std::meta::is_same_type(type, ^^std::string)) {
            return "std::string";
        }
        return std::meta::display_string_of(type);
    }

    consteval std::string_view schema_entity_name(std::meta::info entity) {
        return std::meta::is_base(entity) ? "base" : std::meta::identifier_of(entity);
    }

    consteval void append_schema_bytes(schema_buffer &out, szt size) {
        append_schema_unsigned(out, size);
        out.push('B');
    }

    consteval std::size_t schema_layout_name_width(std::span<const object_field> fields,
                                                   std::meta::info array_entity) {
        std::size_t width = 0;
        for (const auto &field: fields) {
            const auto name = field.entity == array_entity
                ? std::string_view{"item"} : schema_entity_name(field.entity);
            if (name.size() > width) {
                width = name.size();
            }
        }
        return width;
    }

    consteval std::size_t schema_layout_offset_width(std::span<const object_field> fields,
                                                     bool wire) {
        std::size_t width = 0;
        for (const auto &field: fields) {
            const auto value = wire ? field.wire_offset : field.object_offset;
            const auto candidate = schema_unsigned_width(value);
            if (candidate > width) {
                width = candidate;
            }
        }
        return width;
    }

    consteval void append_schema_layout_offsets(schema_buffer &out, const object_field &field,
                                                std::size_t wire_width, std::size_t object_width) {
        if (field.wire_offset == field.object_offset) {
            const auto width = wire_width > object_width ? wire_width : object_width;
            out.append("[wire/object +");
            append_schema_pad(out, schema_unsigned_width(field.wire_offset), width);
            append_schema_unsigned(out, field.wire_offset);
        } else {
            out.append("[wire +");
            append_schema_pad(out, schema_unsigned_width(field.wire_offset), wire_width);
            append_schema_unsigned(out, field.wire_offset);
            out.append(", object +");
            append_schema_pad(out, schema_unsigned_width(field.object_offset), object_width);
            append_schema_unsigned(out, field.object_offset);
        }
        out.append("] ");
    }

    // Array offsets in children are relative to one element, not the leaf payload.
    consteval void append_schema_layout(schema_buffer &out, std::span<const object_field> fields,
                                        std::size_t depth, schema_format_options options,
                                        std::meta::info array_entity = {}) {
        const auto name_width = schema_layout_name_width(fields, array_entity);
        const auto wire_width = options.detailed ? schema_layout_offset_width(fields, true) : 0;
        const auto object_width = options.detailed ? schema_layout_offset_width(fields, false) : 0;
        for (const auto &field: fields) {
            append_schema_indent(out, depth);
            out.append("| ");
            if (options.detailed) {
                append_schema_layout_offsets(out, field, wire_width, object_width);
            }
            const auto name = field.entity == array_entity
                ? std::string_view{"item"} : schema_entity_name(field.entity);
            out.append(name);
            append_schema_pad(out, name.size(), name_width);
            out.append(": ");
            if (field.repeated()) {
                out.append("repeat x");
                append_schema_unsigned(out, field.element_count);
                if (options.detailed) {
                    out.append(" [stride ");
                    append_schema_bytes(out, field.stride);
                    if (!field.children().empty()) {
                        out.append(", offsets relative to item");
                    }
                    out.push(']');
                }
                out.push('\n');
                append_schema_layout(out, field.children(), depth + 1, options, field.entity);
            } else {
                out.append(schema_type_name(field.type));
                out.append(" [");
                append_schema_bytes(out, field.size);
                out.append("]\n");
            }
        }
    }

    consteval void append_schema_leaf(schema_buffer &out, const schema_node &node,
                                     std::meta::info display_type, schema_format_options options) {
        out.append(schema_type_name(display_type));
        out.append(" [");
        if (options.detailed) {
            switch (node.leaf().representation) {
                case leaf_representation::codec:
                    out.append(!node.leaf().scalar() ? "codec"
                        : std::meta::is_enum_type(node.type()) ? "enum" : "scalar");
                    break;
                case leaf_representation::direct: out.append("direct"); break;
                case leaf_representation::segmented: out.append("segmented"); break;
                case leaf_representation::forced: out.append("forced-direct"); break;
            }
            out.append(" leaf, ");
        }
        append_schema_bytes(out, node.encode_extent().value);
        out.push(']');
    }

    consteval void append_schema_node_details(schema_buffer &out, const schema_node &node) {
        if (node.converted() && !std::meta::is_same_type(node.field().memory_type(), node.type())) {
            out.append(" <- ");
            out.append(schema_type_name(std::meta::type_of(node.field().entity)));
        }
        if (node.binding_kind() == schema_binding_kind::record_field) {
            out.append(!node.record_field().fallback_type() ? " [required]" : " [fallback]");
        }
        if (node.kind() == schema_kind::container && node.container().duplicates != schema_duplicates::inherit) {
            out.append(node.container().duplicates == schema_duplicates::reject
                ? " [duplicates: reject]" : " [duplicates: deduplicate]");
        }
    }

    struct memory_span {
        szt begin;
        szt size;
        std::string_view name;
        std::string_view type{};

        [[nodiscard]] consteval std::size_t label_width() const {
            return name.size() + (type.empty() ? 0 : 2 + type.size());
        }
    };

    consteval std::meta::info schema_memory_field_type(const object_field &field) {
        if (field.entity != std::meta::info{}) {
            const auto declared = std::meta::type_of(field.entity);
            // An array element can share the containing array's entity but not its type.
            if (std::meta::is_same_type(declared, field.type)) {
                const auto annotations = std::meta::annotations_of_with_type(field.entity, ^^schema_print_alias_designation);
                if (!annotations.empty()) {
                    const auto alias = std::meta::extract<schema_print_alias_designation>(annotations.front()).type;
                    if (annotations.size() != 1 || !std::meta::is_type_alias(alias)
                        || !std::meta::is_same_type(alias, declared)) {
                        throw "schema_type_alias must specify one alias for the field's declared type";
                    }
                    return alias;
                }
                if (std::meta::is_type_alias(declared)) {
                    return declared;
                }
            }
        }
        return field.type;
    }

    inline constexpr std::size_t maximum_memory_spans = 256;

    consteval void push_memory_span(std::array<memory_span, maximum_memory_spans> &spans,
                                    std::size_t &count, memory_span span) {
        if (count < spans.size()) {
            spans[count++] = span;
        }
    }

    consteval memory_span schema_memory_field_span(const object_field &field, szt origin) {
        const auto name = field.entity == std::meta::info{} ? "item" : schema_entity_name(field.entity);
        const auto field_type = schema_memory_field_type(field);
        auto type = schema_type_name(field_type);
        if (std::meta::is_type_alias(field_type)) {
            // GCC appends the underlying type to an alias's display string.
            type = type.substr(0, type.find(" {aka "));
        }
        return {origin + field.wire_offset, field.size, name, type};
    }

    consteval void sort_memory_spans(std::array<memory_span, maximum_memory_spans> &spans,
                                     std::size_t count) {
        for (std::size_t i = 1; i < count; ++i) {
            const auto key = spans[i];
            std::size_t j = i;
            while (j > 0 && spans[j - 1].begin > key.begin) {
                spans[j] = spans[j - 1];
                --j;
            }
            spans[j] = key;
        }
    }

    // wire_offset drives placement so segmented leaves show their compacted
    // payload instead of the object padding that direct leaves preserve.
    consteval void append_schema_memory_layout(schema_buffer &out, const schema_node &node,
                                                std::size_t exponent, std::size_t depth) {
        const szt payload = node.encode_extent().value;
        if (payload == 0 || payload == std::dynamic_extent) {
            return;
        }
        const szt row_bytes = szt{1} << exponent;

        const auto fields = node.leaf().layout().fields();
        std::array<memory_span, maximum_memory_spans> spans{};
        std::size_t span_count = 0;
        for (const auto &field: fields) {
            if (!field.repeated() || field.children().empty()) {
                push_memory_span(spans, span_count, schema_memory_field_span(field, 0));
            } else {
                for (szt element = 0; element != field.element_count; ++element) {
                    for (const auto &child: field.children()) {
                        push_memory_span(spans, span_count,
                            schema_memory_field_span(child, field.wire_offset + element * field.stride));
                    }
                }
            }
        }
        sort_memory_spans(spans, span_count);

        std::array<memory_span, maximum_memory_spans> regions{};
        std::size_t region_count = 0;
        szt cursor = 0;
        for (std::size_t i = 0; i != span_count; ++i) {
            const auto &span = spans[i];
            if (span.begin > cursor) {
                push_memory_span(regions, region_count, {cursor, span.begin - cursor, "."});
            }
            push_memory_span(regions, region_count, span);
            const auto end = span.begin + span.size;
            if (end > cursor) {
                cursor = end;
            }
        }
        if (cursor < payload) {
            push_memory_span(regions, region_count, {cursor, payload - cursor, "."});
        }

        // Use the coarsest ruler that still marks every possible box edge.
        const auto ruler_end = payload < row_bytes ? payload : row_bytes;
        auto unit_bytes = std::gcd(row_bytes, payload);
        for (std::size_t i = 0; i != region_count; ++i) {
            const auto &region = regions[i];
            unit_bytes = std::gcd(unit_bytes, region.begin);
            unit_bytes = std::gcd(unit_bytes, region.size);
        }
        auto unit_width = schema_unsigned_width(ruler_end) + 2; // B-
        for (std::size_t i = 0; i != region_count; ++i) {
            const auto &region = regions[i];
            if (region.begin >= payload) continue;
            const auto row_remaining = row_bytes - region.begin % row_bytes;
            auto first_size = region.size < row_remaining ? region.size : row_remaining;
            const auto payload_remaining = payload - region.begin;
            if (first_size > payload_remaining) first_size = payload_remaining;
            if (first_size != 0) {
                const auto units = static_cast<std::size_t>(first_size / unit_bytes);
                const auto label_width = region.label_width() + 1 + (first_size == region.size);
                const auto width = (label_width + units - 1) / units;
                if (width > unit_width) {
                    unit_width = width;
                }
            }
        }

        append_schema_indent(out, depth);
        for (szt mark = unit_bytes; mark <= ruler_end; mark += unit_bytes) {
            append_schema_pad(out, schema_unsigned_width(mark) + 2, unit_width);
            append_schema_bytes(out, mark);
            out.push('-');
        }
        out.push('\n');

        for (szt row = 0; row < payload; row += row_bytes) {
            const auto row_end = row + row_bytes < payload ? row + row_bytes : payload;
            append_schema_indent(out, depth);
            const auto line_begin = out.size;
            szt position = row;
            for (std::size_t i = 0; i != region_count; ++i) {
                const auto &region = regions[i];
                const auto end = region.begin + region.size;
                if (end <= position || region.begin >= row_end) {
                    continue;
                }
                const auto from = region.begin > position ? region.begin : position;
                const auto to = end < row_end ? end : row_end;
                const bool starts = from == region.begin;
                const bool ends = to == end;
                const auto width = static_cast<std::size_t>((to - from) / unit_bytes) * unit_width;
                const auto padding = width - starts - ends - (starts ? region.label_width() : 1);
                if (starts) out.push('[');
                out.append(padding / 2, ' ');
                if (starts) {
                    out.append(region.name);
                    if (!region.type.empty()) {
                        out.append(": ");
                        out.append(region.type);
                    }
                } else {
                    out.push('_');
                }
                out.append(padding - padding / 2, ' ');
                if (ends) out.push(']');
                position = to;
            }
            while (out.size > line_begin && out.data[out.size - 1] == ' ') {
                --out.size;
            }
            out.push('\n');
        }
    }

    consteval std::size_t schema_field_id_width(const schema_tree &tree, std::size_t index,
                                                 bool stream) {
        std::size_t width = 0;
        std::size_t position{};
        for (auto child_index: tree.child_indices(index)) {
            const auto candidate = stream
                ? 3 + schema_unsigned_width(position++)
                : 2 + schema_unsigned_width(tree[child_index].record_field().id);
            if (candidate > width) {
                width = candidate;
            }
        }
        return width;
    }

    consteval std::size_t schema_field_name_width(const schema_tree &tree, std::size_t index) {
        std::size_t width = 0;
        for (auto child_index: tree.child_indices(index)) {
            const auto candidate = schema_entity_name(tree[child_index].field().entity).size();
            if (candidate > width) {
                width = candidate;
            }
        }
        return width;
    }

    consteval void append_schema_node(schema_buffer &out, const schema_tree &tree, std::size_t index,
                                      std::meta::info display_type, std::size_t depth,
                                      schema_format_options options) {
        const auto &node = tree[index];
        const bool container = node.kind() == schema_kind::container;
        if (node.kind() == schema_kind::leaf) {
            append_schema_leaf(out, node, display_type, options);
        } else if (container) {
            out.append(node.wire_kind() == entry_kind::map ? "map[" : "sequence[");
            if (node.container().element_count == std::dynamic_extent) {
                out.push('*');
            } else {
                append_schema_unsigned(out, node.container().element_count);
            }
            out.push(']');
            if (depth == 0 || options.detailed || (node.is_field() && node.field().is_base())) {
                out.push(' ');
                out.append(schema_type_name(display_type));
            }
        } else {
            out.append(node.kind() == schema_kind::stream ? "stream " : "record ");
            out.append(schema_type_name(display_type));
        }
        if (options.detailed) {
            append_schema_node_details(out, node);
        }

        if (node.kind() == schema_kind::leaf) {
            // Object layout rows are parts of one leaf payload, not separate wire nodes.
            const auto fields = node.leaf().layout().fields();
            if (node.leaf().representation != leaf_representation::codec
                && !fields.empty()
                && (std::meta::is_class_type(node.type()) || std::meta::is_array_type(node.type()))) {
                out.append(" {\n");
                if (options.memory_layout) {
                    append_schema_memory_layout(out, node, options.memory_layout_exponent,
                                                depth + 1);
                } else {
                    append_schema_layout(out, fields, depth + 1, options);
                }
                append_schema_indent(out, depth);
                out.push('}');
            }
            return;
        }

        out.append(" {\n");
        const auto id_width = container ? 0
            : schema_field_id_width(tree, index, node.kind() == schema_kind::stream);
        const auto name_width = container ? 0 : schema_field_name_width(tree, index);
        std::size_t position{};
        for (auto child_index: tree.child_indices(index)) {
            const auto &child = tree[child_index];
            auto child_display_type = child.type();
            append_schema_indent(out, depth + 1);
            if (container) {
                const auto role = child.slot().role == schema_slot_role::element ? std::string_view{"element"}
                    : child.slot().role == schema_slot_role::key ? std::string_view{"key"}
                    : std::string_view{"value"};
                out.append(role);
                append_schema_pad(out, role.size(), 7);
            } else {
                const auto used = node.kind() == schema_kind::stream
                    ? 3 + schema_unsigned_width(position) : 2 + schema_unsigned_width(child.record_field().id);
                out.push('[');
                if (node.kind() == schema_kind::stream) {
                    out.push('#');
                    append_schema_unsigned(out, position++);
                } else {
                    append_schema_unsigned(out, child.record_field().id);
                }
                out.push(']');
                append_schema_pad(out, used, id_width);
                out.push(' ');
                const auto name = schema_entity_name(child.field().entity);
                out.append(name);
                append_schema_pad(out, name.size(), name_width);
                child_display_type = child.converted()
                    ? child.type() : std::meta::type_of(child.field().entity);
            }
            out.append(": ");
            append_schema_node(out, tree, child_index, child_display_type, depth + 1, options);
            out.push('\n');
        }
        append_schema_indent(out, depth);
        out.push('}');
    }
}

namespace xasrl {
    [[nodiscard]] consteval std::string_view schema_string(std::meta::info type,
                                                           schema_format_options options = {}) {
        const auto tree = detail::build_schema(type);
        detail::schema_buffer result;
        detail::append_schema_node(result, tree, tree.value_index(), type, 0, options);
        const auto text = result.view();
        return {std::define_static_string(text), text.size()};
    }

    [[nodiscard]] consteval std::array<std::string_view, maximum_memory_layout_exponent + 1>
    schema_memory_strings(std::meta::info type) {
        const auto tree = detail::build_schema(type);
        std::array<std::string_view, maximum_memory_layout_exponent + 1> result{};
        for (std::size_t exponent = 0; exponent <= maximum_memory_layout_exponent; ++exponent) {
            detail::schema_buffer out;
            detail::append_schema_node(out, tree, tree.value_index(), type, 0,
                {.memory_layout = true, .memory_layout_exponent = exponent});
            const auto text = out.view();
            result[exponent] = {std::define_static_string(text), text.size()};
        }
        return result;
    }
}

template<std::meta::info Type>
struct std::formatter<xasrl::schema_view<Type>, char> {
    constexpr auto parse(std::format_parse_context &context) {
        auto cursor = context.begin();
        const auto end = context.end();
        while (cursor != end && *cursor != '}') {
            if (*cursor == 'd') {
                if (options.memory_layout) {
                    throw std::format_error("cannot combine 'd' and 'b' in a schema format specifier");
                }
                options.detailed = true;
                ++cursor;
            } else if (*cursor == 'b') {
                if (options.detailed) {
                    throw std::format_error("cannot combine 'd' and 'b' in a schema format specifier");
                }
                options.memory_layout = true;
                ++cursor;
                std::size_t value{};
                bool has_digit = false;
                while (cursor != end && *cursor >= '0' && *cursor <= '9') {
                    const auto digit = static_cast<std::size_t>(*cursor - '0');
                    if (digit > xasrl::maximum_memory_layout_exponent
                        || value > (xasrl::maximum_memory_layout_exponent - digit) / 10) {
                        throw std::format_error("memory layout exponent out of range");
                    }
                    value = value * 10 + digit;
                    ++cursor;
                    has_digit = true;
                }
                if (has_digit) {
                    options.memory_layout_exponent = value;
                }
            } else {
                throw std::format_error("expected an empty specifier, 'd', or 'b<N>' for schema_view");
            }
        }
        return cursor;
    }

    template<typename FormatContext>
    auto format(xasrl::schema_view<Type>, FormatContext &context) const {
        constexpr auto compact = xasrl::schema_string(Type, {});
        constexpr auto detailed = xasrl::schema_string(Type, {.detailed = true});
        constexpr auto layouts = xasrl::schema_memory_strings(Type);

        const auto selected = options.memory_layout
            ? layouts[options.memory_layout_exponent]
            : options.detailed ? detailed : compact;
        return std::formatter<std::string_view>{}.format(selected, context);
    }

private:
    xasrl::schema_format_options options{};
};
