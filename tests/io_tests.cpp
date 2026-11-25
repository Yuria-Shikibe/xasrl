#include "test_support.hpp"

#include <sstream>

namespace srl_test {
    void test_io() {
        const auto path = std::filesystem::temp_directory_path() / "srl_stage1_test.bin";
        std::error_code ignored;
        std::filesystem::remove(path, ignored);

        const Profile profile{
            .person = Person{.age = 28, .score = 77.5, .runtime_cookie = 999},
            .name = "disk block",
            .active = true,
            .state = activity::sleeping,
            .position = Vec2{.x = 3.0f, .y = 6.0f},
        };
        xasrl::byte_buffer packed{};
        require(!xasrl::serialize(profile, packed, default_context), "file encode failed");
        require(!xasrl::write_block(path, packed.span()), "write_block failed");

        const auto block = xasrl::read_block(path);
        require(block.has_value(), "read_block failed");
        require(*block == packed,
                "disk block did not preserve serialized entry bytes");

        Profile loaded{};
        require(!xasrl::deserialize(block->span(), loaded, default_context)
                && loaded.person.age == profile.person.age
                && loaded.name == profile.name
                && loaded.position.y == profile.position.y,
                "file decode failed");

        set_file_byte(path, 0, std::byte{});
        require_error(xasrl::read_block(path),
                      xasrl::error_code::invalid_storage_magic,
                      "invalid storage magic was accepted");

        require(!xasrl::write_block(path, packed.span()), "failed to restore test block");
        set_file_byte(path, 8, std::byte{2});
        require_error(xasrl::read_block(path),
                      xasrl::error_code::unsupported_storage_version,
                      "unsupported storage version was accepted");

        require(!xasrl::write_block(path, packed.span()), "failed to restore test block");
        set_file_byte(path, 12, std::byte{1});
        require_error(xasrl::read_block(path),
                      xasrl::error_code::unsupported_compression,
                      "unsupported compression was accepted");

        std::filesystem::remove(path, ignored);
        require_error(xasrl::read_block(path),
                      xasrl::error_code::io_open_failed,
                      "missing file was accepted");
    }
TEST(StorageIo, RoundTripAndValidation) {
    test_io();
}

TEST(StorageIo, ChunkedRoundTripAndShortIo) {
    xasrl::byte_buffer bytes;
    bytes.resize(2 * xasrl::detail::io_chunk_size + 37, std::byte{0x5a});
    xasrl::detail::write_trivial(bytes.span(), xasrl::detail::wire_v5_magic);
    xasrl::detail::write_trivial(bytes.span().subspan(4), std::uint32_t{});
    xasrl::detail::write_trivial(bytes.span().subspan(8), xasrl::szt{});
    xasrl::detail::write_trivial(bytes.span().subspan(xasrl::detail::document_header_size), xasrl::entry_head{
        .size = bytes.size() - xasrl::detail::document_header_size, .metadata = {0, xasrl::entry_kind::leaf},
    });
    const auto path = std::filesystem::temp_directory_path() / "srl_chunked_test.bin";
    ASSERT_FALSE(xasrl::write_block(path, bytes.span()));
    auto restored = xasrl::read_block(path);
    ASSERT_TRUE(restored);
    EXPECT_EQ(*restored, bytes);
    std::filesystem::remove(path);

    std::ostringstream output;
    ASSERT_FALSE(xasrl::detail::write_bytes(output, bytes.span()));
    auto storage = output.str();
    storage.pop_back();
    std::istringstream input(storage);
    EXPECT_EQ(xasrl::detail::read_bytes(input, restored->span()).code, xasrl::error_code::io_read_failed);

    class short_write_buffer : public std::streambuf {
        std::streamsize xsputn(const char *, std::streamsize count) override { return count - 1; }
    } short_buffer;
    std::ostream short_output(&short_buffer);
    EXPECT_EQ(xasrl::detail::write_bytes(short_output, bytes.span()).code, xasrl::error_code::io_write_failed);
}
}
