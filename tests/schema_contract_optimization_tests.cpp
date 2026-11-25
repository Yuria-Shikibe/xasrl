#include "srl/serialization.hpp"

#include <gtest/gtest.h>
#include <span>
#include <type_traits>

namespace {
    struct [[=xasrl::tag::auto_seq{}]] const_field { const int value = 1; };
    struct [[=xasrl::tag::auto_seq{}]] volatile_field { volatile int value = 1; };
    struct [[=xasrl::tag::auto_seq{}]] ordinary_field { int value = 1; };
    enum class [[=xasrl::tag::leaf{}]] enum_leaf : int { value };
    enum class [[=xasrl::tag::auto_seq{}, =xasrl::tag::auto_seq{}]] enum_record : int { value };

    static_assert(!xasrl::schema_valid(^^const_field));
    static_assert(!xasrl::schema_valid(^^volatile_field));
    static_assert(!xasrl::transactional<const_field>);
    static_assert(xasrl::inspect_schema(^^const_field).reason == xasrl::schema_error::unsupported_entity);
    static_assert(xasrl::schema_diagnostic(^^const_field).contains("const_field::value"));
    static_assert(xasrl::inspect_schema(^^enum_leaf).reason == xasrl::schema_error::invalid_annotation_target);
    static_assert(xasrl::inspect_schema(^^enum_record).reason == xasrl::schema_error::invalid_annotation_target);
    static_assert(xasrl::schema_valid(^^const ordinary_field));

    struct [[=xasrl::tag::auto_stream{}]] byte_pair { unsigned char a, b; };
    struct [[=xasrl::tag::auto_stream{}]] closed_stream { byte_pair a, b; };
    struct [[=xasrl::tag::auto_stream{}]] open_stream { ordinary_field record; };
    static_assert([] consteval {
        const xasrl::schema closed(^^closed_stream);
        const xasrl::schema open(^^open_stream);
        return closed.value_node().decode_extent().value == 52
            && open.value_node().decode_extent().is_dynamic();
    }());

    struct [[=xasrl::tag::auto_seq{}]] base { int value{}; };
    struct [[=xasrl::tag::auto_seq{}]] intermediate : base {};
    struct [[=xasrl::tag::auto_seq{}]] derived : private base, intermediate {
        constexpr derived() = default;
    };

    constexpr auto direct_base = std::meta::bases_of(^^derived, std::meta::access_context::unchecked())[0];
    constexpr auto indirect_base = std::meta::bases_of(^^derived, std::meta::access_context::unchecked())[1];
    static_assert(std::same_as<decltype(xasrl::detail::entity_reference<direct_base,
        xasrl::detail::reflected_entity_kind::base>(std::declval<const derived &>())), const base &>);

    TEST(SchemaContracts, SplicesTheSelectedBaseSubobject) {
        derived source;
        source.[:direct_base:].value = 11;
        source.[:indirect_base:].value = 22;
        xasrl::byte_buffer bytes;
        const xasrl::serialization_context context;
        ASSERT_FALSE(xasrl::serialize(source, bytes, context));
        derived out;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out.[:direct_base:].value, 11);
        EXPECT_EQ(out.[:indirect_base:].value, 22);
    }

    TEST(SchemaContracts, ConstSourceRemainsEncodable) {
        const ordinary_field source{17};
        xasrl::byte_buffer bytes;
        const xasrl::serialization_context context;
        ASSERT_FALSE(xasrl::serialize(source, bytes, context));
        ordinary_field out;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out.value, 17);
    }
}
