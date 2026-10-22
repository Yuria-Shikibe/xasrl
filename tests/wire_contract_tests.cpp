#include "test_support.hpp"

namespace {
    struct integer_field { [[=srl::tag::field{1}]] std::int32_t value{}; };
    struct float_field { [[=srl::tag::field{1}]] float value{}; };
    struct [[=srl::tag::auto_stream{}]] xy { int x{}, y{}; };
    struct [[=srl::tag::auto_stream{}]] yx { int y{}, x{}; };

    static_assert(srl::schema<integer_field>::node(srl::schema<integer_field>::value).wire_type
        == srl::schema<float_field>::node(srl::schema<float_field>::value).wire_type);
    static_assert(srl::schema<xy>::node(srl::schema<xy>::value).wire_type
        != srl::schema<yx>::node(srl::schema<yx>::value).wire_type);

    TEST(WireContract, RejectsSameSizeTypeChangesAndStreamReordering) {
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(integer_field{1065353216}, bytes, context));
        float_field target{42};
        auto status = srl::deserialize(bytes.span(), target, context);
        EXPECT_EQ(status.code, srl::error_code::type_mismatch);
        EXPECT_EQ(status.id, 1u);
        EXPECT_EQ(target.value, 42);

        ASSERT_FALSE(srl::serialize(xy{1, 2}, bytes, context));
        yx stream{3, 4};
        status = srl::deserialize(bytes.span(), stream, context);
        EXPECT_EQ(status.code, srl::error_code::type_mismatch);
        EXPECT_EQ(status.offset, 12u);
        EXPECT_EQ(stream.y, 3);

        ASSERT_FALSE(srl::serialize(std::vector<int>{1065353216}, bytes, context));
        std::vector<float> floats{42};
        EXPECT_EQ(srl::deserialize(bytes.span(), floats, context).code, srl::error_code::type_mismatch);
        EXPECT_EQ(floats, std::vector<float>{42});
    }

    TEST(WireContract, SchemaGeneratesFieldTypeMutationCases) {
        using model = Profile;
        constexpr auto &tree = srl::schema<model>::tree;
        constexpr auto root = tree.child(tree.root, 0);
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(model{}, bytes, context));
        std::size_t ordinal{};
        template for (constexpr auto child: tree.children(root)) {
            constexpr auto expected_type = tree.node(child).wire_type;
            constexpr auto id = tree.node(child).id;
            auto changed = bytes;
            const auto mapping_offset = srl::detail::entry_head_wire_size
                + ordinal++ * srl::detail::entry_mapping_wire_size;
            srl_test::set_u32(changed, mapping_offset + 4, expected_type ^ 0xffffffffu);
            model target;
            const auto status = srl::deserialize(changed.span(), target, context);
            EXPECT_EQ(status.code, srl::error_code::type_mismatch);
            EXPECT_EQ(status.id, id);
        }
    }
}
