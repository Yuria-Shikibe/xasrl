#include "srl/schema.hpp"

#include <array>

struct duplicate {
    [[=srl::tag::field{7}]] int first;
    [[=srl::tag::field{7}]] int second;
};

enum class [[=srl::tag::bit_flag{1}]] bad_flags : unsigned { uncovered = 2 };

struct inner { [[=srl::tag::field{0}]] int *pointer; };
struct outer { [[=srl::tag::field{1}]] inner child; };
struct [[=srl::tag::leaf{}]] enum_object { bad_flags value; };
struct wrapped_object { [[=srl::tag::field{0}]] enum_object child; };
union untagged { int integer; float real; };

struct huge {};
template<> struct srl::leaf_codec<huge> {
    static constexpr srl::szt fixed_size = srl::detail::maximum_entry_size;
    static srl::error encode(const huge &, std::span<std::byte, fixed_size>);
    static srl::error decode(std::span<const std::byte, fixed_size>, huge &);
};

struct half_huge {};
template<> struct srl::leaf_codec<half_huge> {
    static constexpr srl::szt fixed_size = srl::detail::maximum_entry_size / 2;
    static srl::error encode(const half_huge &, std::span<std::byte, fixed_size>);
    static srl::error decode(std::span<const std::byte, fixed_size>, half_huge &);
};
struct overflow_container { [[=srl::tag::field{0}]] std::array<half_huge, 2> items; };

struct [[=srl::tag::auto_seq{}]] duplicate_validation {
    [[=srl::tag::bypass_validation{}, =srl::tag::bypass_validation{}]] bool value;
};
struct [[=srl::tag::leaf{.allow_non_unique_presentation = true}, =srl::tag::bypass_validation{}]] unchecked_union {
    untagged value;
};
struct [[=srl::tag::leaf{}, =srl::tag::bypass_validation{}]] unchecked_bad_flags { bad_flags value; };

struct [[=srl::tag::leaf{}]] float_leaf { float value; };
struct float_record { [[=srl::tag::field{0}]] float_leaf child; };
struct [[=srl::tag::leaf{.allow_non_unique_presentation = true}]] opted_in_float_leaf { float value; };
struct opted_in_float_record { [[=srl::tag::field{0}]] opted_in_float_leaf child; };
struct [[=srl::tag::leaf{}]] nontrivial_float_leaf {
    float value;
    ~nontrivial_float_leaf() {}
};

#if SRL_FAILURE_CASE == 0
static_assert(!srl::schema_valid(^^duplicate));
static_assert(!srl::schema_valid(^^bad_flags));
static_assert(!srl::schema_valid(^^outer));
static_assert(!srl::schema_valid(^^huge));
static_assert(srl::inspect_schema(^^overflow_container).reason == srl::schema_error::extent_overflow);
static_assert(srl::schema_diagnostic(^^overflow_container).contains("overflow_container::items"));
static_assert(srl::schema_diagnostic(^^overflow_container).contains("multiplication overflow"));
static_assert(!srl::schema_valid(^^wrapped_object));
static_assert(srl::inspect_schema(^^duplicate).reason == srl::schema_error::duplicate_field_id);
static_assert(srl::schema_diagnostic(^^duplicate).contains("field id=7"));
static_assert(srl::schema_diagnostic(^^duplicate).contains("duplicate::first"));
static_assert(srl::schema_diagnostic(^^wrapped_object).contains("wrapped_object::child"));
static_assert(srl::schema_diagnostic(^^wrapped_object).contains("enum_object::value"));
static_assert(srl::schema_diagnostic(^^huge).contains("huge"));
static_assert(srl::schema_valid(^^opted_in_float_record));
static_assert(srl::inspect_schema(^^nontrivial_float_leaf).reason
    == srl::schema_error::invalid_object_representation);
static_assert(srl::schema_diagnostic(^^nontrivial_float_leaf).contains("trivially copyable"));
static_assert(!srl::schema_diagnostic(^^nontrivial_float_leaf).contains("allow_non_unique_presentation"));
static_assert(srl::schema_valid(^^int));
constexpr auto result = srl::schema(^^int).tree;
#elif SRL_FAILURE_CASE == 1
constexpr auto result = srl::schema(^^duplicate).tree;
#elif SRL_FAILURE_CASE == 2
constexpr auto result = srl::enum_contains<bad_flags>(1);
#elif SRL_FAILURE_CASE == 3
constexpr auto result = srl::schema(^^outer).tree;
#elif SRL_FAILURE_CASE == 4
constexpr auto result = srl::schema(^^huge).tree;
#elif SRL_FAILURE_CASE == 5
constexpr auto result = srl::detail::make_object_layout(^^untagged);
#elif SRL_FAILURE_CASE == 6
constexpr auto result = srl::schema(^^wrapped_object).tree;
#elif SRL_FAILURE_CASE == 7
constexpr auto result = srl::detail::build_schema(^^duplicate);
#elif SRL_FAILURE_CASE == 8
constexpr auto result = srl::detail::make_object_layout(^^enum_object);
#elif SRL_FAILURE_CASE == 9
constexpr auto result = srl::schema_extent::fixed(std::dynamic_extent);
#elif SRL_FAILURE_CASE == 10
constexpr auto result = srl::schema_extent::fixed(srl::detail::maximum_entry_size) + srl::schema_extent::fixed(1);
#elif SRL_FAILURE_CASE == 11
constexpr auto result = srl::schema_extent::fixed(srl::detail::maximum_entry_size).scaled_by(2);
#elif SRL_FAILURE_CASE == 12
constexpr auto result = srl::schema(^^overflow_container).tree;
#elif SRL_FAILURE_CASE == 13
constexpr auto result = srl::schema(^^duplicate_validation).tree;
#elif SRL_FAILURE_CASE == 14
constexpr auto result = srl::schema(^^unchecked_union).tree;
#elif SRL_FAILURE_CASE == 15
constexpr auto result = srl::schema(^^unchecked_bad_flags).tree;
#elif SRL_FAILURE_CASE == 16
constexpr auto result = srl::schema(^^float_record).tree;
#endif
