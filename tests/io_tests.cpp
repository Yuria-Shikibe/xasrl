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
        srl::byte_buffer packed{};
        require(!srl::serialize(profile, packed, default_context), "file encode failed");
        require(!srl::write_block(path, packed.span()), "write_block failed");

        const auto block = srl::read_block(path);
        require(block.has_value(), "read_block failed");
        require(*block == packed,
                "disk block did not preserve serialized entry bytes");

        Profile loaded{};
        require(!srl::deserialize(block->span(), loaded, default_context)
                && loaded.person.age == profile.person.age
                && loaded.name == profile.name
                && loaded.position.y == profile.position.y,
                "file decode failed");

        set_file_byte(path, 0, std::byte{});
        require_error(srl::read_block(path),
                      srl::error_code::invalid_storage_magic,
                      "invalid storage magic was accepted");

        require(!srl::write_block(path, packed.span()), "failed to restore test block");
        set_file_byte(path, 8, std::byte{2});
        require_error(srl::read_block(path),
                      srl::error_code::unsupported_storage_version,
                      "unsupported storage version was accepted");

        require(!srl::write_block(path, packed.span()), "failed to restore test block");
        set_file_byte(path, 12, std::byte{1});
        require_error(srl::read_block(path),
                      srl::error_code::unsupported_compression,
                      "unsupported compression was accepted");

        std::filesystem::remove(path, ignored);
        require_error(srl::read_block(path),
                      srl::error_code::io_open_failed,
                      "missing file was accepted");
    }
TEST(StorageIo, RoundTripAndValidation) {
    test_io();
}

TEST(StorageIo, ChunkedRoundTripAndShortIo) {
    srl::byte_buffer bytes;
    bytes.resize(2 * srl::detail::io_chunk_size + 37, std::byte{0x5a});
    srl::detail::write_trivial(bytes.span(), srl::entry_head{
        .size = bytes.size(), .metadata = {0, srl::entry_kind::atomic},
    });
    const auto path = std::filesystem::temp_directory_path() / "srl_chunked_test.bin";
    ASSERT_FALSE(srl::write_block(path, bytes.span()));
    auto restored = srl::read_block(path);
    ASSERT_TRUE(restored);
    EXPECT_EQ(*restored, bytes);
    std::filesystem::remove(path);

    std::ostringstream output;
    ASSERT_FALSE(srl::detail::write_bytes(output, bytes.span()));
    auto storage = output.str();
    storage.pop_back();
    std::istringstream input(storage);
    EXPECT_EQ(srl::detail::read_bytes(input, restored->span()).code, srl::error_code::io_read_failed);

    class short_write_buffer : public std::streambuf {
        std::streamsize xsputn(const char *, std::streamsize count) override { return count - 1; }
    } short_buffer;
    std::ostream short_output(&short_buffer);
    EXPECT_EQ(srl::detail::write_bytes(short_output, bytes.span()).code, srl::error_code::io_write_failed);
}
}
