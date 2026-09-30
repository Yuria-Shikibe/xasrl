#include "test_support.hpp"

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
        const auto saved = srl::save(path, profile, default_context);
        require(!saved, "save failed");

        const auto block = srl::read_block(path);
        require(block.has_value(), "read_block failed");
        const auto packed = srl::pack(profile, default_context);
        require(packed.has_value() && *block == *packed,
                "disk block did not preserve serialized entry bytes");

        const auto loaded = srl::load<Profile>(path, default_context);
        require(loaded.has_value()
                && loaded->person.age == profile.person.age
                && loaded->name == profile.name
                && loaded->position.y == profile.position.y,
                "load failed");

        set_file_byte(path, 0, std::byte{});
        require_error(srl::read_block(path),
                      srl::error_code::invalid_storage_magic,
                      "invalid storage magic was accepted");

        require(!srl::save(path, profile, default_context), "failed to restore test block");
        set_file_byte(path, 8, std::byte{1});
        require_error(srl::read_block(path),
                      srl::error_code::unsupported_storage_version,
                      "unsupported storage version was accepted");

        require(!srl::save(path, profile, default_context), "failed to restore test block");
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
}
