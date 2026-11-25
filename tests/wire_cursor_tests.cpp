#include "test_support.hpp"

namespace {
    struct sparse {
        [[=srl::tag::field{1}]] char first{};
        [[=srl::tag::field{100}]] char second{};
        [[=srl::tag::field{120}]] char third{};
    };
}

TEST(WireCursor, MissingFieldDoesNotConsumeTheNextField) {
    using namespace srl;
    byte_buffer bytes;
    ASSERT_FALSE(serialize(sparse{}, bytes, srl_test::default_context));
    const auto doc = srl_test::open_document(bytes.span());
    detail::defined_entry::cursor cursor;
    ASSERT_TRUE(doc.entry.find(1, cursor));
    EXPECT_EQ(doc.entry.find(2, cursor).error().code, error_code::missing_field);
    ASSERT_TRUE(doc.entry.find(100, cursor));
    EXPECT_EQ(doc.entry.find(101, cursor).error().code, error_code::missing_field);
    ASSERT_TRUE(doc.entry.find(120, cursor));
    EXPECT_EQ(doc.entry.find(121, cursor).error().code, error_code::missing_field);
    EXPECT_EQ(cursor.index, 3);
    EXPECT_FALSE(doc.entry.finish(cursor));
}
