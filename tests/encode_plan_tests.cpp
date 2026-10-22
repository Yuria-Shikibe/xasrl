#include "test_support.hpp"

#include <type_traits>
#include <vector>

namespace {
    using tree = srl::schema<std::vector<Rect>>;
    constexpr auto element = tree::child(tree::value, 0);
    static_assert(!srl::detail::encoding_needs_cache(tree::tree, element));
    static_assert(std::is_empty_v<srl::detail::container_slot_plans<tree::tree, element>>);

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
