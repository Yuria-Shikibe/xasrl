#include "test_support.hpp"

namespace {
    namespace d = srl::defaults;
    constexpr auto modify = [](int &value) { value += 3; };
    constexpr auto produce = [] { return 42; };
    constexpr auto produce_context = [](const srl::serialization_context &ctx) {
        return ctx.config.duplicates == srl::duplicate_policy::reject ? 43 : 44;
    };
    constexpr auto modify_context = [](int &value, const srl::serialization_context &ctx) {
        value += produce_context(ctx);
    };
    constexpr auto ambiguous = [](auto &...) { return 1; };
    constexpr auto bad_modifier = [](int &value) { return ++value; };

    static_assert(srl::tag::fallback<>::valid_for<int>);
    static_assert(!srl::tag::fallback<d::value<1.5>>::valid_for<int>);
    static_assert(!srl::tag::fallback<d::modifier<bad_modifier>>::valid_for<int>);
    static_assert(!srl::tag::fallback<d::provider<ambiguous>>::valid_for<int>);
    static_assert(!srl::tag::fallback<d::provider<produce>>::valid_for<double>);

    struct source {
        [[=srl::tag::field{1}]] int present = 7;
    };

    struct target {
        [[=srl::tag::field{1}, =srl::tag::fallback{d::value<99>{}}]] int present = 0;
        [[=srl::tag::field{2}, =srl::tag::fallback{}]] int implicit = 99;
        [[=srl::tag::field{3}, =srl::tag::fallback{}]] int initialized = 99;
        [[=srl::tag::field{4}, =srl::tag::fallback{d::value<12>{}}]] int value = 99;
        [[=srl::tag::field{5}, =srl::tag::fallback{d::modifier<modify>{}}]] int modified = 10;
        [[=srl::tag::field{6}, =srl::tag::fallback{d::provider<produce>{}}]] int produced = 0;
        [[=srl::tag::field{7}, =srl::tag::fallback{d::provider<produce_context>{}}]] int context_value = 0;
        [[=srl::tag::field{8}, =srl::tag::fallback{d::provider<modify>{}}]] int provider_modified = 10;
        [[=srl::tag::field{9}, =srl::tag::fallback{d::provider<modify_context>{}}]] int context_modified = 10;
        [[=srl::tag::field{10}, =srl::tag::fallback{}]] int array[2][2]{{1, 2}, {3, 4}};
    };

    struct duplicate {
        [[=srl::tag::field{1}, =srl::tag::fallback{}, =srl::tag::fallback{d::value<1>{}}]] int value;
    };
    struct invalid_provider {
        [[=srl::tag::field{1}, =srl::tag::fallback{d::provider<ambiguous>{}}]] int value;
    };
    struct [[=srl::tag::auto_stream{}]] invalid_stream {
        [[=srl::tag::fallback{}]] int value;
    };
    struct [[=srl::tag::auto_seq{}]] invalid_transient {
        [[=srl::tag::transient{}, =srl::tag::fallback{}]] int value;
    };
    static_assert(srl::schema_valid<target>());
    static_assert(!srl::schema_valid<duplicate>());
    static_assert(!srl::schema_valid<invalid_provider>());
    static_assert(!srl::schema_valid<invalid_stream>());
    static_assert(!srl::schema_valid<invalid_transient>());

    TEST(Fallback, MissingFieldsAndPresentValues) {
        srl::serialization_context context;
        context.config.duplicates = srl::duplicate_policy::reject;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(source{}, bytes, context));
        target out;
        ASSERT_FALSE(srl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out.present, 7);
        EXPECT_EQ(out.implicit, 0);
        EXPECT_EQ(out.initialized, 0);
        EXPECT_EQ(out.value, 12);
        EXPECT_EQ(out.modified, 13);
        EXPECT_EQ(out.produced, 42);
        EXPECT_EQ(out.context_value, 43);
        EXPECT_EQ(out.provider_modified, 13);
        EXPECT_EQ(out.context_modified, 53);
        EXPECT_EQ(out.array[0][0], 0);
        EXPECT_EQ(out.array[1][1], 0);

        ASSERT_FALSE(srl::serialize(out, bytes, context));
        target copy;
        ASSERT_FALSE(srl::deserialize(bytes.span(), copy, context));
        EXPECT_EQ(copy.modified, 13);
        EXPECT_EQ(copy.context_modified, 53);
    }

    TEST(Fallback, MalformedPresentFieldDoesNotUseFallback) {
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(source{}, bytes, context));
        bytes.resize(bytes.size() - 1);
        target out;
        EXPECT_TRUE(srl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out.present, 0);
    }
}
