#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "../schema/mapping_layout.hpp"
#include "utility.hpp"

namespace xasrl::detail {
    struct field_view {
        id_type id{};
        type_hash_type wire_type{};
        szt offset{};
        std::span<const std::byte> bytes{};
    };
    struct definition_row {
        id_type id;
        type_hash_type wire_type;
        szt leaf_extent;
    };
    static_assert(sizeof(definition_row) == definition_row_size);

    class wire_definition {
        std::span<const std::byte> bytes_;

        explicit wire_definition(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    public:
        static std::expected<wire_definition, error> read(std::span<const std::byte> &input) {
            if (input.size() < definition_header_size) {
                return std::unexpected{fail(error_code::truncated_mapping_table)};
            }
            const auto count = read_trivial<std::uint32_t>(input);
            const auto kind = read_trivial<std::uint32_t>(input.subspan(4));
            if (count > (input.size() - definition_header_size) / definition_row_size) {
                return std::unexpected{fail(error_code::truncated_mapping_table)};
            }
            if ((kind == static_cast<unsigned>(entry_kind::sequence) && count != 1)
                || (kind == static_cast<unsigned>(entry_kind::map) && count != 2)
                || kind < static_cast<unsigned>(entry_kind::stream)
                || kind > static_cast<unsigned>(entry_kind::map)
                || count > entry_metadata::max_mapping_count) {
                return std::unexpected{fail(error_code::invalid_entry_classification)};
            }
            const auto bytes = definition_header_size + count * definition_row_size;
            wire_definition result(input.first(bytes));
            input = input.subspan(bytes);
            for (std::size_t i = 0; i != count; ++i) {
                const auto id = result.row(i).id;
                if ((i != 0 && id <= result.row(i - 1).id)
                    || (result.kind() != entry_kind::record && id != i)) {
                    return std::unexpected{fail(error_code::invalid_mapping_order, id, definition_header_size + i * definition_row_size)};
                }
            }
            return result;
        }

        // Only for canonical, compile-time generated definitions.
        static wire_definition from_schema(std::span<const std::byte> bytes) noexcept { return wire_definition(bytes); }

        [[nodiscard]] std::size_t count() const noexcept {
            return (bytes_.size() - definition_header_size) / definition_row_size;
        }
        [[nodiscard]] entry_kind kind() const noexcept {
            return static_cast<entry_kind>(read_trivial<std::uint32_t>(bytes_.subspan(4)));
        }
        [[nodiscard]] definition_row row(std::size_t i) const noexcept {
            return read_trivial<definition_row>(bytes_.subspan(definition_header_size + i * definition_row_size));
        }
    };

    struct wire_document {
        std::vector<wire_definition> definitions;
        std::span<const std::byte> root;
        std::size_t root_offset{};
        bool schema_only{};

        static std::expected<wire_document, error> read(std::span<const std::byte> bytes, std::optional<szt> fingerprint = std::nullopt) {
            if (bytes.size() < document_header_size) {
                return std::unexpected{fail(error_code::truncated_entry_header)};
            }
            if (read_trivial<std::uint32_t>(bytes) != wire_v5_magic) {
                return std::unexpected{fail(error_code::unsupported_storage_version)};
            }
            const auto count = read_trivial<std::uint32_t>(bytes.subspan(4));
            const auto identity = read_trivial<szt>(bytes.subspan(8));
            wire_document result;
            auto remaining = bytes.subspan(document_header_size);
            result.schema_only = count == schema_only_marker;
            if (result.schema_only) {
                if (fingerprint && identity != *fingerprint) { return std::unexpected{fail(error_code::type_mismatch, std::nullopt, 8)}; }
            } else {
                if (identity != 0) { return std::unexpected{fail(error_code::invalid_entry_classification, std::nullopt, 8)}; }
                if (count > remaining.size() / definition_header_size) {
                    return std::unexpected{fail(error_code::truncated_mapping_table, std::nullopt, document_header_size)};
                }
                result.definitions.reserve(count);
                for (std::uint32_t i = 0; i != count; ++i) {
                    const auto offset = bytes.size() - remaining.size();
                    auto definition = wire_definition::read(remaining);
                    if (!definition) { return std::unexpected{definition.error().rebase(offset)}; }
                    result.definitions.push_back(*definition);
                }
            }
            result.root = remaining;
            result.root_offset = bytes.size() - remaining.size();
            return result;
        }
    };

    class defined_entry {
        entry_head head_;
        std::optional<wire_definition> definition_;
        std::span<const std::byte> content_;
        szt content_offset_;

        defined_entry(entry_head head, std::optional<wire_definition> definition,
                      std::span<const std::byte> content, szt offset) noexcept
            : head_(head), definition_(definition), content_(content), content_offset_(offset) {}

        [[nodiscard]] definition_row row(std::size_t index) const noexcept {
            const auto kind = head_.kind();
            if (kind == entry_kind::sequence || kind == entry_kind::map) {
                auto result = definition_->row(kind == entry_kind::sequence ? 0 : index % 2);
                result.id = static_cast<id_type>(index);
                return result;
            }
            return definition_->row(index);
        }

    public:
        struct cursor {
            std::size_t index{};
            std::size_t consumed{};
        };

        [[nodiscard]] const entry_head &head() const noexcept { return head_; }
        [[nodiscard]] std::span<const std::byte> content() const noexcept { return content_; }
        [[nodiscard]] szt content_offset() const noexcept { return content_offset_; }
        [[nodiscard]] std::size_t count() const noexcept {
            if (head_.kind() == entry_kind::stream) { return definition_->count(); }
            return head_.mapping_count();
        }

        // Called after matching the container kind. Each group has the same rows.
        template<szt Stride, std::size_t Slots>
        [[nodiscard]] bool matches_fixed_slots(const std::array<definition_row, Slots> &expected) const noexcept {
            if constexpr (Stride == 0) {
                if (!content_.empty()) { return false; }
            } else if (content_.size() / Stride != count() / Slots || content_.size() % Stride != 0) {
                return false;
            }
            for (std::size_t i = 0; i != Slots; ++i) {
                const auto actual = definition_->row(i);
                if (actual.wire_type != expected[i].wire_type || actual.leaf_extent != expected[i].leaf_extent) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::expected<field_view, error> next(cursor &state) const {
            const auto def = row(state.index);
            const auto id = head_.kind() == entry_kind::stream ? std::nullopt : std::optional{def.id};
            const auto offset = content_offset_ + state.consumed;
            auto remaining = content_.subspan(state.consumed);
            auto size = def.leaf_extent;
            if (size == std::dynamic_extent) {
                if (remaining.size() < entry_head_wire_size) {
                    return std::unexpected{fail(error_code::truncated_entry_header, id, offset)};
                }
                size = read_trivial<szt>(remaining);
                if (size < entry_head_wire_size) {
                    return std::unexpected{fail(error_code::invalid_entry_size, id, offset)};
                }
            }
            if (size > remaining.size()) {
                return std::unexpected{fail(error_code::payload_out_of_bounds, id, offset)};
            }
            ++state.index;
            state.consumed += static_cast<std::size_t>(size);
            return field_view{def.id, def.wire_type, offset, remaining.first(static_cast<std::size_t>(size))};
        }

        [[nodiscard]] std::expected<field_view, error> find(id_type id, cursor &state) const {
            while (state.index != count() && row(state.index).id < id) {
                if (auto skipped = next(state); !skipped) { return std::unexpected{skipped.error()}; }
            }
            if (state.index == count() || row(state.index).id != id) {
                return std::unexpected{fail(error_code::missing_field, id)};
            }
            return next(state);
        }

        [[nodiscard]] error finish(cursor &state) const {
            while (state.index != count()) {
                if (auto skipped = next(state); !skipped) { return skipped.error(); }
            }
            if (state.consumed != content_.size()) {
                return fail(error_code::trailing_bytes, std::nullopt, content_offset_ + state.consumed);
            }
            return {};
        }

        [[nodiscard]] static std::expected<defined_entry, error> parse(
            std::span<const std::byte> bytes, const wire_document &doc, std::span<const std::byte> local) {
            if (bytes.size() < entry_head_wire_size) {
                return std::unexpected{fail(error_code::truncated_entry_header)};
            }
            const auto head = read_trivial<entry_head>(bytes);
            if (head.size > bytes.size()) { return std::unexpected{fail(error_code::invalid_entry_size)}; }
            if (head.size < bytes.size()) { return std::unexpected{fail(error_code::trailing_bytes, std::nullopt, head.size)}; }
            const auto kind = head.kind();
            const auto count = head.mapping_count();
            if (kind < entry_kind::leaf || kind > entry_kind::map
                || ((kind == entry_kind::leaf || kind == entry_kind::stream) && count != 0)) {
                return std::unexpected{fail(error_code::invalid_entry_classification, std::nullopt, 11)};
            }
            auto content = bytes.subspan(entry_head_wire_size);
            if (kind == entry_kind::leaf) { return defined_entry(head, std::nullopt, content, entry_head_wire_size); }
            if (kind == entry_kind::map && count % 2 != 0) {
                return std::unexpected{fail(error_code::invalid_container_shape, std::nullopt, 8)};
            }
            std::optional<wire_definition> definition;
            if (doc.schema_only) {
                if (local.empty()) { return std::unexpected{fail(error_code::type_mismatch, std::nullopt, 11)}; }
                definition = wire_definition::from_schema(local);
            } else {
                if (content.size() < definition_reference_size) {
                    return std::unexpected{fail(error_code::truncated_mapping_table, std::nullopt, entry_head_wire_size)};
                }
                const auto reference = read_trivial<std::uint32_t>(content);
                content = content.subspan(definition_reference_size);
                if (reference == 0) {
                    auto value = wire_definition::read(content);
                    if (!value) { return std::unexpected{value.error().rebase(entry_head_wire_size + definition_reference_size)}; }
                    definition = *value;
                } else {
                    if (reference > doc.definitions.size()) {
                        return std::unexpected{fail(error_code::invalid_mapping_reference, std::nullopt, entry_head_wire_size)};
                    }
                    definition = doc.definitions[reference - 1];
                }
            }
            if (definition->kind() != kind || (kind == entry_kind::record && definition->count() != count)) {
                return std::unexpected{fail(error_code::invalid_entry_classification, std::nullopt, 8)};
            }
            if (kind == entry_kind::sequence || kind == entry_kind::map) {
                const auto slots = kind == entry_kind::sequence ? 1u : 2u;
                const auto groups = count / slots;
                // The local slot definition gives a lower bound even for dynamic
                // child entries; reject impossible counts before reserving values.
                auto available = static_cast<szt>(content.size());
                for (std::size_t i = 0; i != slots; ++i) {
                    auto width = definition->row(i).leaf_extent;
                    if (width == std::dynamic_extent) { width = entry_head_wire_size; }
                    if (groups && width > available / groups) {
                        return std::unexpected{fail(error_code::invalid_container_shape, std::nullopt, 8)};
                    }
                    available -= width * groups;
                }
            }
            return defined_entry(head, definition, content, bytes.size() - content.size());
        }
    };

    inline error validate_document(std::span<const std::byte> bytes) {
        auto document = wire_document::read(bytes);
        if (!document) { return document.error(); }
        if (document->schema_only) {
            // No writer type is available at the block-I/O boundary.
            const auto root = document->root;
            if (root.size() < entry_head_wire_size) {
                return fail(error_code::truncated_entry_header, std::nullopt, document->root_offset);
            }
            const auto head = read_trivial<entry_head>(root);
            if (head.size > root.size()) { return fail(error_code::invalid_entry_size, std::nullopt, document->root_offset); }
            if (head.size < root.size()) { return fail(error_code::trailing_bytes, std::nullopt, document->root_offset + head.size); }
            if (head.kind() < entry_kind::leaf || head.kind() > entry_kind::map) {
                return fail(error_code::invalid_entry_classification, std::nullopt, document->root_offset + 11);
            }
            return {};
        }
        auto entry = defined_entry::parse(document->root, *document, {});
        if (!entry) { return entry.error().rebase(document->root_offset); }
        if (entry->head().kind() == entry_kind::leaf) { return {}; }
        defined_entry::cursor cursor;
        if (auto status = entry->finish(cursor); status) { return status.rebase(document->root_offset); }
        return {};
    }
}
