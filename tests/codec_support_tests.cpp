#include <gtest/gtest.h>
#include "srl/detail/codec_support.hpp"

namespace {
    TEST(CodecSupport, SizeOverflowDoesNotCommitPartialGrowth) {
        srl::szt size = srl::detail::maximum_entry_size - 8;
        const auto original = size;
        auto status = srl::detail::append_encoded_size(size, 1, 16, 7);
        EXPECT_EQ(status.code, srl::error_code::entry_too_large);
        EXPECT_EQ(size, original);
        status = srl::detail::append_encoded_size(size, 5, 4, 7);
        EXPECT_EQ(status.code, srl::error_code::entry_too_large);
        EXPECT_EQ(size, original);
        EXPECT_FALSE(srl::detail::append_encoded_size(size, 4, 4, 7));
        EXPECT_EQ(size, srl::detail::maximum_entry_size);
    }

    static_assert(srl::detail::resolve_duplicates(srl::schema_duplicates::inherit,
        srl::duplicate_policy::reject) == srl::duplicate_policy::reject);
    static_assert(srl::detail::resolve_duplicates(srl::schema_duplicates::deduplicate,
        srl::duplicate_policy::reject) == srl::duplicate_policy::deduplicate);
    static_assert(srl::detail::resolve_duplicates(srl::schema_duplicates::reject,
        srl::duplicate_policy::deduplicate) == srl::duplicate_policy::reject);
}
