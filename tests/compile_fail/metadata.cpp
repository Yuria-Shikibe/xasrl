#include "srl/schema.hpp"

#include <array>

struct duplicate {
    [[=xasrl::tag::field{7}]] int first;
    [[=xasrl::tag::field{7}]] int second;
};

enum class [[=xasrl::tag::bit_flag{1}]] bad_flags : unsigned { uncovered = 2 };

struct inner { [[=xasrl::tag::field{0}]] int *pointer; };
struct outer { [[=xasrl::tag::field{1}]] inner child; };
struct [[=xasrl::tag::leaf{}]] enum_object { bad_flags value; };
struct wrapped_object { [[=xasrl::tag::field{0}]] enum_object child; };
union untagged { int integer; float real; };

struct huge {};
template<> struct xasrl::leaf_codec<huge> {
    static constexpr xasrl::szt fixed_size = xasrl::detail::maximum_entry_size;
    static xasrl::error encode(const huge &, std::span<std::byte, fixed_size>);
    static xasrl::error decode(std::span<const std::byte, fixed_size>, huge &);
};

struct half_huge {};
template<> struct xasrl::leaf_codec<half_huge> {
    static constexpr xasrl::szt fixed_size = xasrl::detail::maximum_entry_size / 2;
    static xasrl::error encode(const half_huge &, std::span<std::byte, fixed_size>);
    static xasrl::error decode(std::span<const std::byte, fixed_size>, half_huge &);
};
struct overflow_container { [[=xasrl::tag::field{0}]] std::array<half_huge, 2> items; };

struct [[=xasrl::tag::auto_seq{}]] duplicate_validation {
    [[=xasrl::tag::bypass_validation{}, =xasrl::tag::bypass_validation{}]] bool value;
};
struct [[=xasrl::tag::leaf{.allow_non_unique_presentation = true}, =xasrl::tag::bypass_validation{}]] unchecked_union {
    untagged value;
};
struct [[=xasrl::tag::leaf{}, =xasrl::tag::bypass_validation{}]] unchecked_bad_flags { bad_flags value; };

struct [[=xasrl::tag::leaf{}]] float_leaf { float value; };
struct float_record { [[=xasrl::tag::field{0}]] float_leaf child; };
struct [[=xasrl::tag::leaf{.allow_non_unique_presentation = true}]] opted_in_float_leaf { float value; };
struct opted_in_float_record { [[=xasrl::tag::field{0}]] opted_in_float_leaf child; };
struct [[=xasrl::tag::leaf{}]] nontrivial_float_leaf {
    float value;
    ~nontrivial_float_leaf() {}
};

#if SRL_FAILURE_CASE == 0
static_assert(!xasrl::schema_valid(^^duplicate));
static_assert(!xasrl::schema_valid(^^bad_flags));
static_assert(!xasrl::schema_valid(^^outer));
static_assert(!xasrl::schema_valid(^^huge));
static_assert(xasrl::inspect_schema(^^overflow_container).reason == xasrl::schema_error::extent_overflow);
static_assert(xasrl::schema_diagnostic(^^overflow_container).contains("overflow_container::items"));
static_assert(xasrl::schema_diagnostic(^^overflow_container).contains("multiplication overflow"));
static_assert(!xasrl::schema_valid(^^wrapped_object));
static_assert(xasrl::inspect_schema(^^duplicate).reason == xasrl::schema_error::duplicate_field_id);
static_assert(xasrl::schema_diagnostic(^^duplicate).contains("field id=7"));
static_assert(xasrl::schema_diagnostic(^^duplicate).contains("duplicate::first"));
static_assert(xasrl::schema_diagnostic(^^wrapped_object).contains("wrapped_object::child"));
static_assert(xasrl::schema_diagnostic(^^wrapped_object).contains("enum_object::value"));
static_assert(xasrl::schema_diagnostic(^^huge).contains("huge"));
static_assert(xasrl::schema_valid(^^opted_in_float_record));
static_assert(xasrl::inspect_schema(^^nontrivial_float_leaf).reason
    == xasrl::schema_error::invalid_object_representation);
static_assert(xasrl::schema_diagnostic(^^nontrivial_float_leaf).contains("trivially copyable"));
static_assert(!xasrl::schema_diagnostic(^^nontrivial_float_leaf).contains("allow_non_unique_presentation"));
static_assert(xasrl::schema_valid(^^int));
constexpr auto result = xasrl::schema(^^int).tree;
#elif SRL_FAILURE_CASE == 1
constexpr auto result = xasrl::schema(^^duplicate).tree;
#elif SRL_FAILURE_CASE == 2
constexpr auto result = xasrl::enum_contains<bad_flags>(1);
#elif SRL_FAILURE_CASE == 3
constexpr auto result = xasrl::schema(^^outer).tree;
#elif SRL_FAILURE_CASE == 4
constexpr auto result = xasrl::schema(^^huge).tree;
#elif SRL_FAILURE_CASE == 5
constexpr auto result = xasrl::detail::make_object_layout(^^untagged);
#elif SRL_FAILURE_CASE == 6
constexpr auto result = xasrl::schema(^^wrapped_object).tree;
#elif SRL_FAILURE_CASE == 7
constexpr auto result = xasrl::detail::build_schema(^^duplicate);
#elif SRL_FAILURE_CASE == 8
constexpr auto result = xasrl::detail::make_object_layout(^^enum_object);
#elif SRL_FAILURE_CASE == 9
constexpr auto result = xasrl::schema_extent::fixed(std::dynamic_extent);
#elif SRL_FAILURE_CASE == 10
constexpr auto result = xasrl::schema_extent::fixed(xasrl::detail::maximum_entry_size) + xasrl::schema_extent::fixed(1);
#elif SRL_FAILURE_CASE == 11
constexpr auto result = xasrl::schema_extent::fixed(xasrl::detail::maximum_entry_size).scaled_by(2);
#elif SRL_FAILURE_CASE == 12
constexpr auto result = xasrl::schema(^^overflow_container).tree;
#elif SRL_FAILURE_CASE == 13
constexpr auto result = xasrl::schema(^^duplicate_validation).tree;
#elif SRL_FAILURE_CASE == 14
constexpr auto result = xasrl::schema(^^unchecked_union).tree;
#elif SRL_FAILURE_CASE == 15
constexpr auto result = xasrl::schema(^^unchecked_bad_flags).tree;
#elif SRL_FAILURE_CASE == 16
constexpr auto result = xasrl::schema(^^float_record).tree;
#endif
