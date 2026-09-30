#pragma once

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <system_error>

#include "serialization.hpp"

namespace srl {
    namespace detail {
        [[nodiscard]] inline std::error_code current_io_error() noexcept {
            return {errno, std::generic_category()};
        }
    }

    [[nodiscard]] inline error write_block(
        const std::filesystem::path &path,
        std::span<const std::byte> entry_bytes) {
        if (auto entry = detail::parse_entry(entry_bytes); !entry) {
            return entry.error();
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return detail::fail(error_code::io_open_failed, 0, 0, detail::current_io_error());
        }

        constexpr storage_head head{
            .magic_number = detail::storage_magic,
            .version = detail::storage_version,
            .compress_tag__cap_only = detail::compression_none
        };

        std::array<std::byte, detail::storage_head_wire_size> header{};
        detail::write_trivial(std::span{header}, head);
        output.write(reinterpret_cast<const char *>(header.data()),
                     static_cast<std::streamsize>(header.size()));
        output.write(reinterpret_cast<const char *>(entry_bytes.data()),
                     static_cast<std::streamsize>(entry_bytes.size()));
        if (!output) {
            return detail::fail(error_code::io_write_failed, 0, 0, detail::current_io_error());
        }
        output.flush();
        if (!output) {
            return detail::fail(error_code::io_flush_failed, 0, 0, detail::current_io_error());
        }
        output.close();
        if (output.fail()) {
            return detail::fail(error_code::io_write_failed, 0, 0, detail::current_io_error());
        }
        return {};
    }

    [[nodiscard]] inline std::expected<byte_buffer, error> read_block(
        const std::filesystem::path &path) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input.is_open()) {
            return std::unexpected{detail::fail(error_code::io_open_failed, 0, 0, detail::current_io_error())};
        }

        const auto end = input.tellg();
        if (end < 0) {
            return std::unexpected{detail::fail(error_code::io_seek_failed, 0, 0, detail::current_io_error())};
        }
        const auto file_size = static_cast<std::uint64_t>(end);
        if (file_size < detail::storage_head_wire_size) {
            return std::unexpected{detail::fail(error_code::invalid_storage_magic)};
        }
        if (file_size > static_cast<std::uint64_t>(detail::storage_head_wire_size)
                            + std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected{detail::fail(error_code::file_too_large)};
        }

        input.seekg(0, std::ios::beg);
        if (!input) {
            return std::unexpected{detail::fail(error_code::io_seek_failed, 0, 0, detail::current_io_error())};
        }

        std::array<std::byte, detail::storage_head_wire_size> header{};
        input.read(reinterpret_cast<char *>(header.data()),
                   static_cast<std::streamsize>(header.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(header.size())) {
            return std::unexpected{detail::fail(error_code::io_read_failed, 0, 0, detail::current_io_error())};
        }
        const auto storage = detail::read_trivial<storage_head>(
            std::span<const std::byte>{header});

        if (storage.magic_number != detail::storage_magic) {
            return std::unexpected{detail::fail(error_code::invalid_storage_magic)};
        }
        if (storage.version != detail::storage_version) {
            return std::unexpected{detail::fail(error_code::unsupported_storage_version)};
        }
        if (storage.compress_tag__cap_only != detail::compression_none) {
            return std::unexpected{detail::fail(error_code::unsupported_compression)};
        }

        byte_buffer entry_bytes;
        entry_bytes.resize(static_cast<std::size_t>(file_size - detail::storage_head_wire_size));
        if (!entry_bytes.empty()) {
            input.read(reinterpret_cast<char *>(entry_bytes.data()),
                       static_cast<std::streamsize>(entry_bytes.size()));
            if (!input || input.gcount() != static_cast<std::streamsize>(entry_bytes.size())) {
                return std::unexpected{detail::fail(error_code::io_read_failed, 0, 0, detail::current_io_error())};
            }
        }
        if (auto entry = detail::parse_entry(entry_bytes.span()); !entry) {
            return std::unexpected{entry.error()};
        }
        return entry_bytes;
    }

    template<serializable T>
    [[nodiscard]] error save(
        const std::filesystem::path &path,
        const T &value,
        const serialization_context &context) {
        auto bytes = pack(value, context);
        if (!bytes) {
            return bytes.error();
        }
        return write_block(path, bytes->span());
    }

    template<typename T>
        requires serializable<T> && std::default_initializable<codec_value_type<T>>
    [[nodiscard]] std::expected<codec_value_type<T>, error> load(
        const std::filesystem::path &path,
        const serialization_context &context) {
        auto bytes = read_block(path);
        if (!bytes) {
            return std::unexpected{bytes.error()};
        }
        return extract<T>(bytes->span(), context);
    }
}
