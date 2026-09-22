#pragma once

#include <gtest/gtest.h>

#include "test_models.hpp"

namespace srl_test {
    inline const srl::serialization_context default_context;
    inline const srl::serialization_context rejecting_context{
        srl::duplicate_policy::reject};

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
}
