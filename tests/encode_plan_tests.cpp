#include "test_support.hpp"

#include <type_traits>
#include <vector>

namespace {
    struct [[=srl::tag::auto_seq{}]] fixed_record {
        int value{};
    };
    struct [[=srl::tag::auto_stream{}]] fixed_stream {
        int value{};
    };

    template<typename T>
    using root_plan = typename srl::detail::root_codec<T>::encode_plan;
    template<typename T>
    using payload_plan = decltype(root_plan<T>::payload);

    static_assert(std::is_empty_v<decltype(payload_plan<fixed_record>::size)>);
    static_assert(std::is_empty_v<decltype(payload_plan<fixed_stream>::size)>);
    static_assert(std::is_empty_v<decltype(root_plan<int>::size)>);
    static_assert(sizeof(payload_plan<fixed_record>) == 1);
    static_assert(sizeof(payload_plan<fixed_stream>) == 1);
    // Identical empty size members in a nested root/payload need distinct addresses.
    static_assert(sizeof(root_plan<fixed_record>) <= 2);
    static_assert(sizeof(root_plan<int>) == 1);
    using fixed_array_plan = payload_plan<std::array<int, 3>>;
    static_assert(sizeof(fixed_array_plan) == 1);
    static_assert(std::is_empty_v<decltype(fixed_array_plan::slot_count)>);
    static_assert(fixed_array_plan{}.slot_count == 3);
    static_assert(sizeof(decltype(payload_plan<std::vector<int>>::size)) == sizeof(srl::szt));
    static_assert(sizeof(decltype(payload_plan<std::vector<int>>::slot_count)) == sizeof(std::uint32_t));

    TEST(EncodePlan, ConditionalSizesAndCountsPreserveWireData) {
        const srl::serialization_context context;
        auto round_trip = [&]<typename T>(const T &source) {
            root_plan<T> plan;
            ASSERT_FALSE(plan.prepare(source, context));
            srl::byte_buffer bytes;
            ASSERT_FALSE(srl::serialize(source, bytes, context));
            EXPECT_EQ(static_cast<srl::szt>(plan.size), bytes.size());
            T restored{};
            ASSERT_FALSE(srl::deserialize(bytes.span(), restored, context));
            EXPECT_EQ(restored, source);
        };
        round_trip(42);
        round_trip(std::array<int, 3>{1, 2, 3});
        round_trip(std::array<int, 0>{});
        round_trip(std::array<std::string, 2>{"short", "longer text"});
        round_trip(std::vector<int>{1, 2, 3});
        round_trip(std::vector<int>{});
        round_trip(std::map<int, std::string>{{1, "first"}, {2, "second"}});
    }

    constexpr srl::schema tree(^^std::vector<Rect>);
    constexpr auto element = tree.tree.child(tree.value, 0);
    static_assert(!srl::detail::encoding_needs_cache(tree.tree, element));
    static_assert(std::is_empty_v<srl::detail::container_slot_plans<tree.tree, element>>);

    struct [[=srl::tag::auto_seq{}]] many_fields {
        int a, b, c, d, e, f, g, h, i, j, k, l;
    };
    constexpr srl::schema many_schema(^^many_fields);
    using many_plan = srl::detail::value_codec<many_schema.tree, many_schema.value>::encode_plan;
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
