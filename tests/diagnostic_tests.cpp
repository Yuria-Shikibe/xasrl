#include "test_support.hpp"

namespace {
    struct inner {
        [[=srl::tag::field{0}]] bool flag{};
    };
    struct outer {
        [[=srl::tag::field{5}]] inner value;
    };

    TEST(Diagnostics, NestedOffsetsAndZeroFieldId) {
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(outer{}, bytes, context));
        const auto entry = srl::detail::parse_entry(bytes.span());
        ASSERT_TRUE(entry);
        const auto field = entry->at(0);
        const auto nested = srl::detail::parse_entry(field.bytes);
        ASSERT_TRUE(nested);
        const auto position = field.offset + nested->at(0).offset;
        bytes[position] = std::byte{2};
        outer out;
        const auto status = srl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(status.code, srl::error_code::invalid_bool);
        EXPECT_EQ(status.offset, position);
        ASSERT_TRUE(status.id);
        EXPECT_EQ(*status.id, 0u);

        bytes[position] = std::byte{0};
        bytes[field.offset + 11] = std::byte{0};
        const auto header = srl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(header.code, srl::error_code::invalid_entry_classification);
        EXPECT_EQ(header.offset, field.offset + 11);
        EXPECT_EQ(header.id, 5u);
    }

    TEST(Diagnostics, LeafRootOffsetIncludesHeader) {
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(false, bytes, context));
        bytes[srl::detail::entry_head_wire_size] = std::byte{2};
        bool out = false;
        const auto status = srl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(status.code, srl::error_code::invalid_bool);
        EXPECT_EQ(status.offset, srl::detail::entry_head_wire_size);
        EXPECT_FALSE(status.id);
    }
}
