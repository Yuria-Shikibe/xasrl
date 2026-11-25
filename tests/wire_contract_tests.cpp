#include "test_support.hpp"

namespace {
    struct integer_field { [[=xasrl::tag::field{1}]] std::int32_t value{}; };
    struct float_field { [[=xasrl::tag::field{1}]] float value{}; };
    struct [[=xasrl::tag::auto_stream{}]] xy { int x{}, y{}; };
    struct [[=xasrl::tag::auto_stream{}]] yx { int y{}, x{}; };

    static_assert(xasrl::schema(^^integer_field).value_node().wire_type()
        == xasrl::schema(^^float_field).value_node().wire_type());
    static_assert(xasrl::schema(^^xy).value_node().wire_type()
        != xasrl::schema(^^yx).value_node().wire_type());

    TEST(WireContract, RejectsSameSizeTypeChangesAndStreamReordering) {
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(integer_field{1065353216}, bytes, context));
        float_field target{42};
        auto status = xasrl::deserialize(bytes.span(), target, context);
        EXPECT_EQ(status.code, xasrl::error_code::type_mismatch);
        EXPECT_EQ(status.id, 1u);
        EXPECT_EQ(target.value, 42);

        ASSERT_FALSE(xasrl::serialize(xy{1, 2}, bytes, context));
        yx stream{3, 4};
        status = xasrl::deserialize(bytes.span(), stream, context);
        EXPECT_EQ(status.code, xasrl::error_code::type_mismatch);
        EXPECT_EQ(status.offset, 28u);
        EXPECT_EQ(stream.y, 3);

        ASSERT_FALSE(xasrl::serialize(std::vector<int>{1065353216}, bytes, context));
        std::vector<float> floats{42};
        EXPECT_EQ(xasrl::deserialize(bytes.span(), floats, context).code, xasrl::error_code::type_mismatch);
        EXPECT_EQ(floats, std::vector<float>{42});
    }

    TEST(WireContract, SchemaGeneratesFieldTypeMutationCases) {
        using model = Profile;
        constexpr auto tree = xasrl::schema(^^model).tree;
        constexpr auto root = tree.value_index();
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(model{}, bytes, context));
        std::size_t ordinal{};
        template for (constexpr auto child: tree.child_indices(root)) {
            constexpr auto expected_type = tree[child].wire_type();
            constexpr auto id = tree[child].record_field().id;
            auto changed = bytes;
            const auto mapping_offset = srl_test::open_document(bytes.span()).inline_definition_offset() + xasrl::detail::definition_header_size + ordinal++ * xasrl::detail::definition_row_size;
            srl_test::set_u32(changed, mapping_offset + 4, expected_type ^ 0xffffffffu);
            model target;
            const auto status = xasrl::deserialize(changed.span(), target, context);
            EXPECT_EQ(status.code, xasrl::error_code::type_mismatch);
            EXPECT_EQ(status.id, id);
        }
    }
}
