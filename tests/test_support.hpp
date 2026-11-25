#pragma once

#include <gtest/gtest.h>

#include "test_models.hpp"

namespace srl_test {
    inline const srl::serialization_context default_context;
    inline const srl::serialization_context rejecting_context{
        srl::serialization_config{
            .duplicates = srl::duplicate_policy::reject,
        }};

    inline void require(bool condition, std::string_view description) {
        if (!condition) {
            throw std::runtime_error(std::string{description});
        }
    }

    template<typename T>
    void require_error(const std::expected<T, srl::error> &result,
                       srl::error_code expected,
                       std::string_view description) {
        require(!result && result.error().code == expected, description);
    }

    inline void require_error(srl::error result, srl::error_code expected,
                              std::string_view description) {
        require(result.code == expected, description);
    }

    inline void set_u32(srl::byte_buffer &buffer, std::size_t offset, std::uint32_t value) {
        srl::detail::write_trivial(buffer.span().subspan(offset), value);
    }

    [[nodiscard]] inline std::uint32_t get_u32(const srl::byte_buffer &buffer,
                                               std::size_t offset) {
        return srl::detail::read_trivial<std::uint32_t>(buffer.span().subspan(offset));
    }

    inline void set_file_byte(const std::filesystem::path &path,
                              std::size_t offset,
                              std::byte value) {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        require(file.is_open(), "failed to open test block for mutation");
        file.seekp(static_cast<std::streamoff>(offset));
        const auto character = static_cast<char>(std::to_integer<unsigned char>(value));
        file.write(&character, 1);
        require(static_cast<bool>(file), "failed to mutate test block");
    }

    inline void set_u64(srl::byte_buffer &buffer, std::size_t offset, srl::szt value) {
        srl::detail::write_trivial(buffer.span().subspan(offset), value);
    }

    [[nodiscard]] inline srl::szt get_u64(const srl::byte_buffer &buffer, std::size_t offset) {
        return srl::detail::read_trivial<srl::szt>(buffer.span().subspan(offset));
    }

    struct document_view {
        srl::detail::wire_document document;
        srl::detail::defined_entry entry;

        srl::detail::field_view at(std::size_t index) const {
            srl::detail::defined_entry::cursor cursor;
            for (std::size_t i = 0; i != index; ++i) {
                if (!entry.next(cursor)) { throw std::runtime_error("invalid preceding field"); }
            }
            auto field = entry.next(cursor).value();
            field.offset += document.root_offset;
            return field;
        }

        std::size_t root_offset() const noexcept { return document.root_offset; }
        std::size_t inline_definition_offset() const noexcept {
            return document.root_offset + srl::detail::entry_head_wire_size + srl::detail::definition_reference_size;
        }
    };

    inline document_view open_document(std::span<const std::byte> bytes) {
        auto document = srl::detail::wire_document::read(bytes, 0).value();
        auto entry = srl::detail::defined_entry::parse(document.root, document, {}).value();
        return {std::move(document), entry};
    }
}
