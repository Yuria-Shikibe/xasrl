#include "test_support.hpp"

#include "srl/number_to_string.hpp"

namespace {
    TEST(AtomicFile, ReplacesExistingFileAndPreservesItOnCommitFailure) {
        const auto pid = xasrl::number_to_string<10, wchar_t>(GetCurrentProcessId());
        const auto directory = std::filesystem::temp_directory_path()
            / std::wstring{L"srl_atomic_"}.append(pid.view());
        std::filesystem::create_directory(directory);
        const auto path = directory / "save.bin";
        xasrl::serialization_context context;
        xasrl::byte_buffer first, second;
        ASSERT_FALSE(xasrl::serialize(11, first, context));
        ASSERT_FALSE(xasrl::serialize(22, second, context));
        ASSERT_FALSE(xasrl::write_block(path, first.span()));
        ASSERT_FALSE(xasrl::write_block(path, second.span()));
        ASSERT_EQ(*xasrl::read_block(path), second);

        // A real Windows handle denies delete/rename while still permitting reads.
        auto locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        ASSERT_NE(locked, INVALID_HANDLE_VALUE);
        const auto status = xasrl::write_block(path, first.span());
        EXPECT_TRUE(CloseHandle(locked));
        EXPECT_EQ(status.code, xasrl::error_code::io_commit_failed);
        ASSERT_TRUE(status.system_error);
        EXPECT_NE(status.system_error->value(), 0);
        const auto retained = xasrl::read_block(path);
        ASSERT_TRUE(retained);
        EXPECT_EQ(*retained, second);

        {
            auto abandoned = xasrl::detail::staged_file::create(path);
            ASSERT_TRUE(abandoned);
            ASSERT_FALSE(abandoned->write(first.span().first(3)));
        }
        EXPECT_EQ(*xasrl::read_block(path), second);
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory),
            std::filesystem::directory_iterator{}), 1);
        EXPECT_TRUE(std::filesystem::remove(path));
        EXPECT_TRUE(std::filesystem::remove(directory));
    }
}
