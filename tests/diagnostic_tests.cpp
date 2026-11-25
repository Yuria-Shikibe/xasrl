#include "test_support.hpp"

namespace {
    struct invalid_member_target {
        [[=srl::tag::field{1}, =srl::tag::auto_seq{}]] int value{};
    };

    struct [[=srl::tag::auto_seq{}, =srl::tag::fallback{}]] invalid_type_target {
        int value{};
    };

    struct [[=srl::tag::auto_seq{}]] invalid_base_target
        : [[=srl::conversion_tag{^^int}]] invalid_member_target {};

    enum class invalid_enumerator_target { value [[=srl::tag::bit_flag{}]] };

    struct [[=srl::tag::auto_seq{}]] invalid_static_target {
        [[=srl::tag::field{1}]] static inline int ignored{};
        int value{};
    };

    struct object_part { invalid_enumerator_target value; };
    struct [[=srl::tag::leaf{}]] object_with_invalid_target { object_part part; };

    struct [[=srl::tag::target::enumerator]] enum_hint {};
    enum class [[=srl::tag::bit_flag{}]] valid_enum_target : unsigned { value [[=enum_hint{}, =42]] = 1 };

    static_assert(srl::inspect_schema(^^invalid_member_target).reason == srl::schema_error::invalid_annotation_target);
    static_assert(srl::inspect_schema(^^invalid_type_target).reason == srl::schema_error::invalid_annotation_target);
    static_assert(srl::inspect_schema(^^invalid_base_target).reason == srl::schema_error::invalid_annotation_target);
    static_assert(srl::inspect_schema(^^invalid_enumerator_target).reason == srl::schema_error::invalid_annotation_target);
    static_assert(srl::inspect_schema(^^invalid_static_target).reason == srl::schema_error::invalid_annotation_target);
    static_assert(srl::inspect_schema(^^object_with_invalid_target).reason == srl::schema_error::invalid_annotation_target);
    static_assert(srl::schema_diagnostic(^^invalid_member_target).contains("invalid_member_target::value"));
    static_assert(srl::schema_diagnostic(^^invalid_member_target).contains("srl::tag::auto_seq"));
    static_assert(srl::schema_valid(^^valid_enum_target));

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
        const auto doc = srl_test::open_document(bytes.span());
        const auto field = doc.at(0);
        const auto nested = srl::detail::defined_entry::parse(field.bytes, doc.document, {});
        ASSERT_TRUE(nested);
        srl::detail::defined_entry::cursor cursor;
        const auto position = field.offset + nested->next(cursor)->offset;
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
        bytes.span().back() = std::byte{2};
        bool out = false;
        const auto status = srl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(status.code, srl::error_code::invalid_bool);
        EXPECT_EQ(status.offset, bytes.size() - 1);
        EXPECT_FALSE(status.id);
    }
}
