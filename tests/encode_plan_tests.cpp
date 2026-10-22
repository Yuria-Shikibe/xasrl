#include "test_support.hpp"

#include <type_traits>
#include <vector>

namespace {
    using tree = srl::schema<std::vector<Rect>>;
    constexpr auto element = tree::child(tree::value, 0);
    static_assert(!srl::detail::encoding_needs_cache(tree::tree, element));
    static_assert(std::is_empty_v<srl::detail::container_slot_plans<tree::tree, element>>);

    struct [[=srl::tag::auto_seq{}]] many_fields {
        int a, b, c, d, e, f, g, h, i, j, k, l;
    };
    using many_schema = srl::schema<many_fields>;
    using many_plan = srl::detail::value_codec<many_schema::tree, many_schema::value>::encode_plan;
    static_assert(std::meta::identifier_of(many_plan::members[10]) == "field_10");
    static_assert(std::meta::identifier_of(many_plan::members[11]) == "field_11");

    TEST(EncodePlan, FixedRecordsNeedNoPerElementCache) {
        const std::vector<Rect> values{{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(values, bytes, context));
        std::vector<Rect> restored;
        ASSERT_FALSE(srl::deserialize(bytes.span(), restored, context));
        ASSERT_EQ(restored.size(), values.size());
        for (std::size_t index = 0; index != values.size(); ++index) {
            EXPECT_EQ(restored[index].minimum.x, values[index].minimum.x);
            EXPECT_EQ(restored[index].minimum.y, values[index].minimum.y);
            EXPECT_EQ(restored[index].maximum.x, values[index].maximum.x);
            EXPECT_EQ(restored[index].maximum.y, values[index].maximum.y);
        }
    }
}
