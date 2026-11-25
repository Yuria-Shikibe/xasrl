#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <ostream>
#include <span>
#include <system_error>

#include "detail/wire_document.hpp"
#include "storage/byte_buffer.hpp"
#include "limits.hpp"
#include "detail/staged_file.hpp"

namespace xasrl {
    namespace detail {
        constexpr std::size_t io_chunk_size = 1024 * 1024;
        static_assert(io_chunk_size <= std::numeric_limits<std::streamsize>::max());

        [[nodiscard]] inline std::error_code current_io_error() noexcept {
            return {errno, std::generic_category()};
        }

        [[nodiscard]] inline error write_bytes(std::ostream &output, std::span<const std::byte> bytes) {
            while (!bytes.empty()) {
                const auto count = std::min(bytes.size(), io_chunk_size);
                output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(count));
                if (!output) {
                    return fail(error_code::io_write_failed, std::nullopt, 0, current_io_error());
                }
                bytes = bytes.subspan(count);
            }
            return {};
        }

        [[nodiscard]] inline error read_bytes(std::istream &input, std::span<std::byte> bytes) {
            while (!bytes.empty()) {
                const auto count = std::min(bytes.size(), io_chunk_size);
                input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(count));
                if (!input || input.gcount() != static_cast<std::streamsize>(count)) {
                    return fail(error_code::io_read_failed, std::nullopt, 0, current_io_error());
                }
                bytes = bytes.subspan(count);
            }
            return {};
        }
    }

    [[nodiscard]] inline error write_block(
        const std::filesystem::path &path,
        std::span<const std::byte> entry_bytes) {
        if (auto status = detail::validate_document(entry_bytes); status) { return status; }

        auto output = detail::staged_file::create(path);
        if (!output) { return output.error(); }

        constexpr storage_head head{
            .magic_number = detail::storage_magic,
            .version = detail::storage_version,
            .compress_tag__cap_only = detail::compression_none
        };

        std::array<std::byte, detail::storage_head_wire_size> header{};
        detail::write_trivial(std::span{header}, head);
        if (auto status = output->write(header); status) { return status; }
        if (auto status = output->write(entry_bytes); status) { return status; }
        return std::move(*output).commit();
    }

    [[nodiscard]] inline std::expected<byte_buffer, error> read_block(
        const std::filesystem::path &path, szt max_entry_bytes = decode_limits{}.max_input_bytes) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input.is_open()) {
            return std::unexpected{detail::fail(error_code::io_open_failed, std::nullopt, 0, detail::current_io_error())};
        }

        const auto end = input.tellg();
        if (end < 0) {
            return std::unexpected{detail::fail(error_code::io_seek_failed, std::nullopt, 0, detail::current_io_error())};
        }
        const auto file_size = static_cast<szt>(end);
        if (file_size < detail::storage_head_wire_size) {
            return std::unexpected{detail::fail(error_code::invalid_storage_magic)};
        }
        const auto entry_size = file_size - detail::storage_head_wire_size;
        if (entry_size > max_entry_bytes) {
            return std::unexpected{detail::fail(error_code::resource_limit_exceeded)};
        }
        byte_buffer entry_bytes;
        if (entry_size > detail::maximum_entry_size || entry_size > entry_bytes.max_size()) {
            return std::unexpected{detail::fail(error_code::file_too_large)};
        }

        input.seekg(0, std::ios::beg);
        if (!input) {
            return std::unexpected{detail::fail(error_code::io_seek_failed, std::nullopt, 0, detail::current_io_error())};
        }

        std::array<std::byte, detail::storage_head_wire_size> header{};
        if (auto status = detail::read_bytes(input, header); status) { return std::unexpected{status}; }
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

        entry_bytes.resize_uninitialized(static_cast<std::size_t>(entry_size));
        if (auto status = detail::read_bytes(input, entry_bytes.span()); status) { return std::unexpected{status}; }
        if (auto status = detail::validate_document(entry_bytes.span()); status) { return std::unexpected{status}; }
        return entry_bytes;
    }

}
