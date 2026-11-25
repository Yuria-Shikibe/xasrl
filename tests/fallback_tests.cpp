#include <memory_resource>

#include "test_support.hpp"

namespace {
    namespace d = xasrl::defaults;
    constexpr auto modify = [](int &value) { value += 3; };
    constexpr auto produce = [] { return 42; };
    constexpr auto produce_context = [](const xasrl::serialization_context &ctx) {
        return ctx.config.duplicates == xasrl::duplicate_policy::reject ? 43 : 44;
    };
    constexpr auto modify_context = [](int &value, const xasrl::serialization_context &ctx) {
        value += produce_context(ctx);
    };
    constexpr auto ambiguous = [](auto &...) { return 1; };
    constexpr auto bad_modifier = [](int &value) { return ++value; };

    static_assert(xasrl::tag::fallback<>::valid_for<int>);
    static_assert(!xasrl::tag::fallback<d::value<1.5>>::valid_for<int>);
    static_assert(!xasrl::tag::fallback<d::modifier<bad_modifier>>::valid_for<int>);
    static_assert(!xasrl::tag::fallback<d::provider<ambiguous>>::valid_for<int>);
    static_assert(!xasrl::tag::fallback<d::provider<produce>>::valid_for<double>);

    struct source {
        [[=xasrl::tag::field{1}]] int present = 7;
    };

    struct target {
        [[=xasrl::tag::field{1}, =xasrl::tag::fallback{d::value<99>{}}]] int present = 0;
        [[=xasrl::tag::field{2}, =xasrl::tag::fallback{}]] int implicit = 99;
        [[=xasrl::tag::field{3}, =xasrl::tag::fallback{}]] int initialized = 99;
        [[=xasrl::tag::field{4}, =xasrl::tag::fallback{d::value<12>{}}]] int value = 99;
        [[=xasrl::tag::field{5}, =xasrl::tag::fallback{d::modifier<modify>{}}]] int modified = 10;
        [[=xasrl::tag::field{6}, =xasrl::tag::fallback{d::provider<produce>{}}]] int produced = 0;
        [[=xasrl::tag::field{7}, =xasrl::tag::fallback{d::provider<produce_context>{}}]] int context_value = 0;
        [[=xasrl::tag::field{8}, =xasrl::tag::fallback{d::provider<modify>{}}]] int provider_modified = 10;
        [[=xasrl::tag::field{9}, =xasrl::tag::fallback{d::provider<modify_context>{}}]] int context_modified = 10;
        [[=xasrl::tag::field{10}, =xasrl::tag::fallback{}]] int array[2][2]{{1, 2}, {3, 4}};
    };

    struct duplicate {
        [[=xasrl::tag::field{1}, =xasrl::tag::fallback{}, =xasrl::tag::fallback{d::value<1>{}}]] int value;
    };
    struct invalid_provider {
        [[=xasrl::tag::field{1}, =xasrl::tag::fallback{d::provider<ambiguous>{}}]] int value;
    };
    struct [[=xasrl::tag::auto_stream{}]] invalid_stream {
        [[=xasrl::tag::fallback{}]] int value;
    };
    struct [[=xasrl::tag::auto_seq{}]] invalid_transient {
        [[=xasrl::tag::transient{}, =xasrl::tag::fallback{}]] int value;
    };
    static_assert(xasrl::schema_valid(^^target));
    static_assert(!xasrl::schema_valid(^^duplicate));
    static_assert(!xasrl::schema_valid(^^invalid_provider));
    static_assert(!xasrl::schema_valid(^^invalid_stream));
    static_assert(!xasrl::schema_valid(^^invalid_transient));

    TEST(Fallback, MissingFieldsAndPresentValues) {
        xasrl::serialization_context context;
        context.config.duplicates = xasrl::duplicate_policy::reject;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(source{}, bytes, context));
        target out;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
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

        ASSERT_FALSE(xasrl::serialize(out, bytes, context));
        target copy;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), copy, context));
        EXPECT_EQ(copy.modified, 13);
        EXPECT_EQ(copy.context_modified, 53);
    }

    TEST(Fallback, MalformedPresentFieldDoesNotUseFallback) {
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(source{}, bytes, context));
        bytes.resize(bytes.size() - 1);
        target out;
        EXPECT_TRUE(xasrl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out.present, 0);
    }

    class [[=xasrl::tag::auto_seq{}]] private_default {
        int value_;

    public:
        constexpr explicit private_default(int value = 13) : value_(value) {}
        constexpr int value() const { return value_; }
    };

    struct [[=xasrl::tag::auto_seq{}]] structural_default {
        int number{};
        bool active{};
    };

    struct automatic_target {
        [[=xasrl::tag::field{1}, =xasrl::tag::use_initializer]] int present = 99;
        [[=xasrl::tag::field{2}, =xasrl::tag::use_initializer]] int hp = 100;
        [[=xasrl::tag::field{3}, =xasrl::tag::use_initializer]] int matrix[2][2]{{1, 2}, {3, 4}};
        [[=xasrl::tag::field{4}, =xasrl::tag::use_initializer]] std::string name = "a default name exceeding the short string buffer";
        [[=xasrl::tag::field{5}, =xasrl::tag::use_initializer]] std::vector<std::string> labels{"one", "a second label exceeding the short string buffer"};
        [[=xasrl::tag::field{6}, =xasrl::tag::use_initializer]] std::array<int, 3> values{5, 6, 7};
        [[=xasrl::tag::field{7}, =xasrl::tag::use_initializer]] structural_default object{9, true};
    };

    class private_owner {
        [[=xasrl::tag::field{2}, =xasrl::tag::use_initializer]] int hp_ = 25;

    public:
        constexpr private_owner() = default;
        int hp() const { return hp_; }
        void set_hp(int hp) { hp_ = hp; }
    };

    struct nonstructural_default_owner {
        [[=xasrl::tag::field{2}, =xasrl::tag::use_initializer]] private_default value{25};
    };

    struct constructor_defaults {
        static inline int runtime_constructions{};
        [[=xasrl::tag::field{2}, =xasrl::tag::use_initializer]] int overridden = 100;
        [[=xasrl::tag::field{3}, =xasrl::tag::use_initializer]] int constructed;

        constexpr constructor_defaults() : overridden(7), constructed(73) {
            if !consteval { ++runtime_constructions; }
        }
    };

    int runtime_initial_value() { return 12; }

    struct runtime_default_owner {
        int runtime_only = runtime_initial_value();
        [[=xasrl::tag::field{2}, =xasrl::tag::use_initializer]] int hp = 100;
    };

    struct deleted_default_owner {
        deleted_default_owner() = delete;
        [[=xasrl::tag::field{2}, =xasrl::tag::use_initializer]] int hp = 100;
    };

    struct potentially_throwing_destructor {
        [[=xasrl::tag::field{2}, =xasrl::tag::use_initializer]] int hp = 100;
        constexpr ~potentially_throwing_destructor() noexcept(false) {}
    };

    struct converted_count {
        static inline int conversions{};
        int value = 19;
        constexpr converted_count() = default;
        constexpr explicit converted_count(int stored) : value(stored - 1000) {
            if !consteval { ++conversions; }
        }
        constexpr explicit operator int() const {
            if !consteval { ++conversions; }
            return value + 1000;
        }
    };

    struct converted_default {
        [[=xasrl::tag::field{2}, =xasrl::tag::use_initializer, =xasrl::conversion_tag{^^int}]]
        converted_count count;
    };

    struct [[=xasrl::tag::auto_stream{}]] automatic_stream {
        [[=xasrl::tag::use_initializer]] int value = 7;
    };
    struct [[=xasrl::tag::leaf{}]] automatic_leaf {
        [[=xasrl::tag::use_initializer]] int value = 7;
    };
    struct [[=xasrl::tag::auto_seq{}]] automatic_transient {
        [[=xasrl::tag::transient{}, =xasrl::tag::use_initializer]] int value = 7;
    };

    struct self_reference {
        struct link { const int *address; };
        int value = 7;
        int *pointer = &value;
        std::array<link, 1> fixed{{{&value}}};
        std::vector<link> dynamic{{&value}};
    };

    struct allocating_owner {
        std::vector<int> scratch{1, 2, 3};
        [[=xasrl::tag::field{2}, =xasrl::tag::use_initializer]] int hp = 100;
    };

    struct nested_defaults {
        [[=xasrl::tag::field{2}, =xasrl::tag::use_initializer]] int hp = 25;
    };

    struct nested_source {
        [[=xasrl::tag::field{1}]] source child;
    };

    struct runtime_outer {
        int runtime_only = runtime_initial_value();
        [[=xasrl::tag::field{1}]] nested_defaults child;
    };

    struct automatic_then_required {
        [[=xasrl::tag::field{0}, =xasrl::tag::use_initializer]] int hp = 100;
        [[=xasrl::tag::field{2}]] int required = 77;
    };

    struct boolean_source {
        [[=xasrl::tag::field{1}]] bool enabled = true;
    };

    struct automatic_then_boolean {
        [[=xasrl::tag::field{0}, =xasrl::tag::use_initializer]] int hp = 100;
        [[=xasrl::tag::field{1}, =xasrl::tag::use_initializer]] bool enabled = true;
    };

    static_assert(xasrl::schema_valid(^^automatic_target));
    static_assert(xasrl::schema_valid(^^constructor_defaults));
    static_assert(xasrl::schema_valid(^^allocating_owner));
    static_assert(xasrl::schema_valid(^^private_owner));
    static_assert(xasrl::schema_valid(^^runtime_outer));
    static_assert(xasrl::schema_valid(^^potentially_throwing_destructor));
    static_assert(xasrl::schema_valid(^^converted_default));
    static_assert(!xasrl::schema_valid(^^runtime_default_owner));
    static_assert(!xasrl::schema_valid(^^deleted_default_owner));
    static_assert(!xasrl::schema_valid(^^nonstructural_default_owner));
    static_assert(!xasrl::schema_valid(^^automatic_stream));
    static_assert(!xasrl::schema_valid(^^automatic_leaf));
    static_assert(!xasrl::schema_valid(^^automatic_transient));
    static_assert(!xasrl::detail::automatic_fallback_valid<^^self_reference::pointer>);
    static_assert(!xasrl::detail::automatic_fallback_valid<^^self_reference::fixed>);
    static_assert(!xasrl::detail::automatic_fallback_valid<^^self_reference::dynamic>);
    static_assert(xasrl::inspect_schema(^^runtime_default_owner).reason == xasrl::schema_error::invalid_fallback);
    static_assert(xasrl::schema_diagnostic(^^runtime_default_owner).contains("automatic"));
    static_assert(xasrl::schema_diagnostic(^^runtime_default_owner).contains("constant expression"));

    TEST(Fallback, AutomaticDefaultsIncludeArraysStringsAndContainers) {
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(source{}, bytes, context));
        automatic_target out;
        out.hp = 1;
        out.matrix[1][1] = 9;
        out.name.clear();
        out.labels.clear();
        out.values.fill(0);
        out.object = {};
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out.present, 7);
        EXPECT_EQ(out.hp, 100);
        EXPECT_EQ(out.matrix[0][0], 1);
        EXPECT_EQ(out.matrix[0][1], 2);
        EXPECT_EQ(out.matrix[1][0], 3);
        EXPECT_EQ(out.matrix[1][1], 4);
        EXPECT_EQ(out.name, "a default name exceeding the short string buffer");
        EXPECT_EQ(out.labels, (std::vector<std::string>{"one", "a second label exceeding the short string buffer"}));
        EXPECT_EQ(out.values, (std::array<int, 3>{5, 6, 7}));
        EXPECT_EQ(out.object.number, 9);
        EXPECT_TRUE(out.object.active);

        out.hp = 3;
        out.matrix[1][1] = 9;
        xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
        ASSERT_FALSE(xasrl::serialize(out, bytes, xasrl::srl_context{context, recorder}));
        automatic_target copy;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), copy, context));
        EXPECT_EQ(copy.hp, 3);
        EXPECT_EQ(copy.matrix[1][1], 9);

        ASSERT_FALSE(xasrl::serialize(source{}, bytes, context));
        private_owner private_out;
        private_out.set_hp(1);
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), private_out, context));
        EXPECT_EQ(private_out.hp(), 25);
    }

    TEST(Fallback, AutomaticUsesConstructorResultsWithoutRuntimeConstruction) {
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(source{}, bytes, context));
        constructor_defaults out;
        out.overridden = 1;
        out.constructed = 2;
        constructor_defaults::runtime_constructions = 0;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out.overridden, 7);
        EXPECT_EQ(out.constructed, 73);
        EXPECT_EQ(constructor_defaults::runtime_constructions, 0);

        allocating_owner allocating;
        allocating.hp = 1;
        allocating.scratch = {8, 9};
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), allocating, context));
        EXPECT_EQ(allocating.hp, 100);
        EXPECT_EQ(allocating.scratch, (std::vector<int>{8, 9}));
    }

    TEST(Fallback, AutomaticNestedDefaultsDoNotConstructOuterObject) {
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(nested_source{}, bytes, context));
        runtime_outer out;
        out.runtime_only = 88;
        out.child.hp = 1;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out.child.hp, 25);
        EXPECT_EQ(out.runtime_only, 88);
    }

    TEST(Fallback, AutomaticAppliesToMemoryBeforeStorageConversion) {
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(source{}, bytes, context));
        converted_default out;
        out.count.value = 1;
        converted_count::conversions = 0;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out.count.value, 19);
        EXPECT_EQ(converted_count::conversions, 0);

        out.count.value = 37;
        ASSERT_FALSE(xasrl::serialize(out, bytes, context));
        converted_default copy;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), copy, context));
        EXPECT_EQ(copy.count.value, 37);
        EXPECT_EQ(converted_count::conversions, 2);
    }

    TEST(Fallback, AutomaticDoesNotMakeOtherFieldsOptional) {
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(source{}, bytes, context));
        automatic_then_required out{1, 2};
        const auto status = xasrl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(status.code, xasrl::error_code::missing_field);
        EXPECT_EQ(status.id, 2u);
        EXPECT_EQ(out.hp, 1);
        EXPECT_EQ(out.required, 2);
    }

    TEST(Fallback, AutomaticDoesNotRepairMalformedPresentFields) {
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(boolean_source{}, bytes, context));
        bytes.span().back() = std::byte{2};
        automatic_then_boolean out{1, false};
        const auto status = xasrl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(status.code, xasrl::error_code::invalid_bool);
        EXPECT_EQ(status.id, 1u);
        EXPECT_EQ(out.hp, 1);
        EXPECT_FALSE(out.enabled);
    }
}
