#include <gtest/gtest.h>
#include "srl/detail/codec_support.hpp"

namespace {
    TEST(CodecSupport, SizeOverflowDoesNotCommitPartialGrowth) {
        xasrl::szt size = xasrl::detail::maximum_entry_size - 8;
        const auto original = size;
        auto status = xasrl::detail::append_encoded_size(size, 1, 16, 7);
        EXPECT_EQ(status.code, xasrl::error_code::entry_too_large);
        EXPECT_EQ(size, original);
        status = xasrl::detail::append_encoded_size(size, 5, 4, 7);
        EXPECT_EQ(status.code, xasrl::error_code::entry_too_large);
        EXPECT_EQ(size, original);
        EXPECT_FALSE(xasrl::detail::append_encoded_size(size, 4, 4, 7));
        EXPECT_EQ(size, xasrl::detail::maximum_entry_size);
    }

    static_assert(xasrl::detail::resolve_duplicates(xasrl::schema_duplicates::inherit,
        xasrl::duplicate_policy::reject) == xasrl::duplicate_policy::reject);
    static_assert(xasrl::detail::resolve_duplicates(xasrl::schema_duplicates::deduplicate,
        xasrl::duplicate_policy::reject) == xasrl::duplicate_policy::deduplicate);
    static_assert(xasrl::detail::resolve_duplicates(xasrl::schema_duplicates::reject,
        xasrl::duplicate_policy::deduplicate) == xasrl::duplicate_policy::reject);
}
