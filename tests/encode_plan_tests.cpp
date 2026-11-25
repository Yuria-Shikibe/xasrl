#include "test_support.hpp"

#include <memory_resource>
#include <type_traits>
#include <vector>

namespace {
    struct [[=xasrl::tag::auto_seq{}]] fixed_record {
        int value{};
    };
    struct [[=xasrl::tag::auto_stream{}]] fixed_stream {
        int value{};
    };

    template<typename T>
    using root_plan = typename xasrl::detail::root_codec<T>::encode_plan;
    template<typename T>
    using payload_plan = decltype(root_plan<T>::payload);

    static_assert(sizeof(payload_plan<fixed_record>) == 1);
    static_assert(sizeof(payload_plan<fixed_stream>) == 1);
    static_assert(sizeof(root_plan<fixed_record>) <= 2);
    static_assert(sizeof(root_plan<int>) == 1);
    static_assert(std::is_empty_v<payload_plan<std::array<int, 3>>>);
    static_assert(std::is_empty_v<payload_plan<std::vector<int>>>);
    static_assert(std::is_empty_v<payload_plan<std::vector<std::vector<int>>>>);
    static_assert(sizeof(root_plan<std::vector<int>>) == sizeof(xasrl::szt));

    TEST(EncodePlan, ConditionalSizesAndCountsPreserveWireData) {
        const xasrl::serialization_context context;
        auto round_trip = [&]<typename T>(const T &source) {
            xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
            const auto prepared = xasrl::prepared_serialization<T>::prepare(recorder, source, context);
            ASSERT_TRUE(prepared);
            xasrl::byte_buffer bytes;
            bytes.resize(prepared->size());
            ASSERT_FALSE(prepared->encode(bytes.span()));
            T restored{};
            ASSERT_FALSE(xasrl::deserialize(bytes.span(), restored, context));
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

    constexpr xasrl::schema tree(^^std::vector<Rect>);
    constexpr auto element = tree.tree.child_indices(tree.tree.value_index())[0];
    static_assert(!xasrl::detail::encoding_needs_cache(tree.tree, element));

    struct [[=xasrl::tag::auto_seq{}]] many_fields {
        int a, b, c, d, e, f, g, h, i, j, k, l;
    };
    constexpr xasrl::schema many_schema(^^many_fields);
    using many_plan = xasrl::detail::value_codec<many_schema.tree, many_schema.tree.value_index()>::encode_plan;
    static_assert(std::meta::identifier_of(many_plan::members[10]) == "field_10");
    static_assert(std::meta::identifier_of(many_plan::members[11]) == "field_11");

    TEST(EncodePlan, FixedRecordsNeedNoPerElementCache) {
        const std::vector<Rect> values{{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(values, bytes, context));
        std::vector<Rect> restored;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), restored, context));
        ASSERT_EQ(restored.size(), values.size());
        for (std::size_t index = 0; index != values.size(); ++index) {
            EXPECT_EQ(restored[index].minimum.x, values[index].minimum.x);
            EXPECT_EQ(restored[index].minimum.y, values[index].minimum.y);
            EXPECT_EQ(restored[index].maximum.x, values[index].maximum.x);
            EXPECT_EQ(restored[index].maximum.y, values[index].maximum.y);
        }
    }
}
