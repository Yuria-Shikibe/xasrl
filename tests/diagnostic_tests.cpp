#include "test_support.hpp"

namespace {
    struct invalid_member_target {
        [[=xasrl::tag::field{1}, =xasrl::tag::auto_seq{}]] int value{};
    };

    struct [[=xasrl::tag::auto_seq{}, =xasrl::tag::fallback{}]] invalid_type_target {
        int value{};
    };

    struct [[=xasrl::tag::auto_seq{}]] invalid_base_target
        : [[=xasrl::conversion_tag{^^int}]] invalid_member_target {};

    enum class invalid_enumerator_target { value [[=xasrl::tag::bit_flag{}]] };

    struct [[=xasrl::tag::auto_seq{}]] invalid_static_target {
        [[=xasrl::tag::field{1}]] static inline int ignored{};
        int value{};
    };

    struct object_part { invalid_enumerator_target value; };
    struct [[=xasrl::tag::leaf{}]] object_with_invalid_target { object_part part; };

    struct [[=xasrl::tag::target::enumerator]] enum_hint {};
    enum class [[=xasrl::tag::bit_flag{}]] valid_enum_target : unsigned { value [[=enum_hint{}, =42]] = 1 };

    static_assert(xasrl::inspect_schema(^^invalid_member_target).reason == xasrl::schema_error::invalid_annotation_target);
    static_assert(xasrl::inspect_schema(^^invalid_type_target).reason == xasrl::schema_error::invalid_annotation_target);
    static_assert(xasrl::inspect_schema(^^invalid_base_target).reason == xasrl::schema_error::invalid_annotation_target);
    static_assert(xasrl::inspect_schema(^^invalid_enumerator_target).reason == xasrl::schema_error::invalid_annotation_target);
    static_assert(xasrl::inspect_schema(^^invalid_static_target).reason == xasrl::schema_error::invalid_annotation_target);
    static_assert(xasrl::inspect_schema(^^object_with_invalid_target).reason == xasrl::schema_error::invalid_annotation_target);
    static_assert(xasrl::schema_diagnostic(^^invalid_member_target).contains("invalid_member_target::value"));
    static_assert(xasrl::schema_diagnostic(^^invalid_member_target).contains("xasrl::tag::auto_seq"));
    static_assert(xasrl::schema_valid(^^valid_enum_target));

    struct inner {
        [[=xasrl::tag::field{0}]] bool flag{};
    };
    struct outer {
        [[=xasrl::tag::field{5}]] inner value;
    };

    TEST(Diagnostics, NestedOffsetsAndZeroFieldId) {
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(outer{}, bytes, context));
        const auto doc = srl_test::open_document(bytes.span());
        const auto field = doc.at(0);
        const auto nested = xasrl::detail::defined_entry::parse(field.bytes, doc.document, {});
        ASSERT_TRUE(nested);
        xasrl::detail::defined_entry::cursor cursor;
        const auto position = field.offset + nested->next(cursor)->offset;
        bytes[position] = std::byte{2};
        outer out;
        const auto status = xasrl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(status.code, xasrl::error_code::invalid_bool);
        EXPECT_EQ(status.offset, position);
        ASSERT_TRUE(status.id);
        EXPECT_EQ(*status.id, 0u);

        bytes[position] = std::byte{0};
        bytes[field.offset + 11] = std::byte{0};
        const auto header = xasrl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(header.code, xasrl::error_code::invalid_entry_classification);
        EXPECT_EQ(header.offset, field.offset + 11);
        EXPECT_EQ(header.id, 5u);
    }

    TEST(Diagnostics, LeafRootOffsetIncludesHeader) {
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(false, bytes, context));
        bytes.span().back() = std::byte{2};
        bool out = false;
        const auto status = xasrl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(status.code, xasrl::error_code::invalid_bool);
        EXPECT_EQ(status.offset, bytes.size() - 1);
        EXPECT_FALSE(status.id);
    }
}
