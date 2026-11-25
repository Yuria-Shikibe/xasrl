#pragma once

#pragma push_macro("NOMINMAX")
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#pragma pop_macro("NOMINMAX")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include "../number_to_string.hpp"
#include "utility.hpp"

namespace srl::detail {
    [[nodiscard]] inline error windows_io_error(error_code code, DWORD native_error = GetLastError()) noexcept {
        return fail(code, std::nullopt, 0, std::error_code{static_cast<int>(native_error), std::system_category()});
    }

    // The temporary is on the destination volume. No target bytes are touched
    // before the final rename; an abandoned operation owns only its temporary.
    class staged_file {
        std::filesystem::path destination_;
        std::filesystem::path temporary_;
        HANDLE handle_ = INVALID_HANDLE_VALUE;
        bool owns_temporary_ = true;

        staged_file(std::filesystem::path destination, std::filesystem::path temporary, HANDLE handle) noexcept
            : destination_(std::move(destination)), temporary_(std::move(temporary)), handle_(handle) {}

    public:
        staged_file(const staged_file &) = delete;
        staged_file &operator=(const staged_file &) = delete;
        staged_file &operator=(staged_file &&) = delete;

        staged_file(staged_file &&other) noexcept
            : destination_(std::move(other.destination_)), temporary_(std::move(other.temporary_)),
              handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)),
              owns_temporary_(std::exchange(other.owns_temporary_, false)) {}

        ~staged_file() {
            if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); }
            // Cleanup cannot replace the primary write/commit error. A crash or
            // failed deletion may leave this uniquely named temporary behind.
            if (owns_temporary_) { DeleteFileW(temporary_.c_str()); }
        }

        [[nodiscard]] static std::expected<staged_file, error> create(const std::filesystem::path &destination) {
            static std::atomic<std::uint64_t> sequence{};
            static const auto epoch = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto pid = number_to_string<10, wchar_t>(GetCurrentProcessId());
            const auto timestamp = number_to_string<10, wchar_t>(epoch);
            const auto index = number_to_string<10, wchar_t>(sequence.fetch_add(1, std::memory_order_relaxed));
            auto temporary = destination;
            temporary += std::wstring{L".srl-"}.append(pid.view()).append(L"-").append(timestamp.view())
                .append(L"-").append(index.view()).append(L".tmp");
            // Copy before acquiring the handle: all potentially allocating path
            // operations precede ownership acquisition.
            auto target = destination;
            const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                return std::unexpected{windows_io_error(error_code::io_open_failed)};
            }
            return staged_file{std::move(target), std::move(temporary), handle};
        }

        [[nodiscard]] error write(std::span<const std::byte> bytes) {
            while (!bytes.empty()) {
                const auto count = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1024 * 1024));
                DWORD written{};
                if (!WriteFile(handle_, bytes.data(), count, &written, nullptr)) {
                    return windows_io_error(error_code::io_write_failed);
                }
                if (written != count) {
                    return windows_io_error(error_code::io_write_failed, ERROR_WRITE_FAULT);
                }
                bytes = bytes.subspan(count);
            }
            return {};
        }

        [[nodiscard]] error commit() && {
            if (!FlushFileBuffers(handle_)) {
                return windows_io_error(error_code::io_flush_failed);
            }
            if (!CloseHandle(std::exchange(handle_, INVALID_HANDLE_VALUE))) {
                return windows_io_error(error_code::io_write_failed);
            }
            // No COPY_ALLOWED: the replacement must remain a same-volume rename.
            if (!MoveFileExW(temporary_.c_str(), destination_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                return windows_io_error(error_code::io_commit_failed);
            }
            owns_temporary_ = false;
            return {};
        }
    };
}
