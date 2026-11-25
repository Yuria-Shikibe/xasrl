#include "test_support.hpp"
#include "srl/diagnostics.hpp"
#include "srl/serialization.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    enum class [[=xasrl::tag::bit_flag{0}]] permissions : std::uint8_t {
        read = 1, write = 2, special = 0x80,
    };
    enum class ordinary : std::uint8_t { read = 1, write = 2 };
    enum class [[=xasrl::tag::bit_flag{.mask = 1}]] signed_flags : int { read = 1 };
    enum class [[=xasrl::tag::bit_flag{.mask = 256}]] oversized_flags : std::uint8_t {};
    enum class [[=xasrl::tag::bit_flag{.mask = 1}]] uncovered_flags : unsigned { write = 2 };
    enum class [[=xasrl::tag::bit_flag{1}, =xasrl::tag::bit_flag{1}]] duplicate_flags : unsigned { read = 1 };
    enum class [[=xasrl::tag::bit_flag{}]] zero_flags : unsigned {};
    enum class [[=xasrl::tag::bit_flag{0}]] only_zero_flags : unsigned { none = 0 };
    enum class [[=xasrl::tag::bit_flag{.mask = 1ull << 63}]] wide_flags : std::uint64_t { high = 1ull << 63 };

    static_assert(xasrl::schema_valid(^^permissions));
    static_assert(!xasrl::schema_valid(^^signed_flags));
    static_assert(!xasrl::schema_valid(^^oversized_flags));
    static_assert(!xasrl::schema_valid(^^uncovered_flags));
    static_assert(!xasrl::schema_valid(^^duplicate_flags));
    static_assert(xasrl::enum_contains<permissions>(0));
    static_assert(xasrl::enum_contains<permissions>(3));
    static_assert(xasrl::enum_contains<permissions>(0x83));
    static_assert(!xasrl::enum_contains<permissions>(4));
    static_assert(!xasrl::enum_contains<ordinary>(3));
    static_assert(!xasrl::schema_valid(^^zero_flags));
    static_assert(!xasrl::schema_valid(^^only_zero_flags));
    static_assert(xasrl::enum_contains<wide_flags>(1ull << 63));

    struct record { [[=xasrl::tag::field{0}]] permissions value{}; };
    struct [[=xasrl::tag::leaf{}]] object { permissions value{}; };
    struct forced { [[=xasrl::tag::field{0}, =xasrl::tag::leaf{}]] object value; };
    struct invalid_inner { [[=xasrl::tag::field{2}]] int *pointer{}; };
    struct invalid_outer { [[=xasrl::tag::field{1}]] invalid_inner child; };
    struct [[=xasrl::tag::leaf{}]] invalid_leaf {
        [[=xasrl::tag::fallback{}]] unsigned value{};
    };
    struct leaf_parent { [[=xasrl::tag::field{1}]] invalid_leaf child; };
    struct [[=xasrl::tag::leaf{}]] invalid_enum_object { uncovered_flags value{}; };
    struct [[=xasrl::tag::auto_seq{}]] empty_record {};

    static_assert(!xasrl::schema_valid(^^invalid_enum_object));
    static_assert(!xasrl::schema_valid(^^leaf_parent));
    static_assert(!xasrl::leaf_serializable<invalid_leaf>);
    constexpr auto issue = xasrl::inspect_schema(^^invalid_outer);
    static_assert(issue.reason == xasrl::schema_error::unsupported_entity);
    static_assert(issue.path.size() == 3);
    static_assert(issue.path[0] == ^^invalid_outer);
    static_assert(issue.path[1] == ^^invalid_outer::child);
    static_assert(issue.path[2] == ^^invalid_inner::pointer);
    static_assert(xasrl::schema_diagnostic(^^invalid_outer).contains("child"));
    static_assert(xasrl::schema_diagnostic(^^invalid_outer).contains("pointer"));
    static_assert(!std::is_aggregate_v<xasrl::detail::defined_entry>);
    static_assert(!std::is_default_constructible_v<xasrl::detail::defined_entry>);
    static_assert(!std::is_constructible_v<xasrl::detail::defined_entry, xasrl::entry_head,
        std::span<const std::byte>, std::span<const std::byte>, xasrl::szt>);
    static_assert(std::is_const_v<std::remove_reference_t<decltype(
        std::declval<xasrl::detail::defined_entry &>().head())>>);

    TEST(Flags, RootRecordAndObjectRepresentation) {
        const xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        for (const auto value: {0, 1, 2, 3, 0x80, 0x83}) {
            const auto source = static_cast<permissions>(value);
            ASSERT_FALSE(xasrl::serialize(source, bytes, context));
            auto restored = permissions::read;
            ASSERT_FALSE(xasrl::deserialize(bytes.span(), restored, context));
            EXPECT_EQ(restored, source);
        }
        auto round_trip = [&]<typename T>(T source, auto read) {
            ASSERT_FALSE(xasrl::serialize(source, bytes, context));
            T target{};
            ASSERT_FALSE(xasrl::deserialize(bytes.span(), target, context));
            EXPECT_EQ(read(target), static_cast<permissions>(3));
            bytes[bytes.size() - 1] = std::byte{4};
            const auto status = xasrl::deserialize(bytes.span(), target, context);
            EXPECT_EQ(status.code, xasrl::error_code::invalid_enum_value);
            EXPECT_EQ(read(target), static_cast<permissions>(3));
            EXPECT_EQ(status.offset, bytes.size() - 1);
        };
        round_trip(record{static_cast<permissions>(3)}, [](const record &v) { return v.value; });
        round_trip(object{static_cast<permissions>(3)}, [](const object &v) { return v.value; });
        round_trip(forced{{static_cast<permissions>(3)}}, [](const forced &v) { return v.value.value; });
    }

    TEST(Flags, ContainerValidationAndOrdinaryEnumsRemainStrict) {
        const xasrl::serialization_context context;
        const std::vector<permissions> source{permissions::read, static_cast<permissions>(3)};
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(source, bytes, context));
        std::vector<permissions> target;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), target, context));
        EXPECT_EQ(target, source);
        bytes[bytes.size() - 1] = std::byte{4};
        EXPECT_EQ(xasrl::deserialize(bytes.span(), target, context).code, xasrl::error_code::invalid_enum_value);
        EXPECT_EQ(target, source);

        ASSERT_FALSE(xasrl::serialize(ordinary::read, bytes, context));
        bytes[bytes.size() - 1] = std::byte{3};
        auto value = ordinary::write;
        EXPECT_EQ(xasrl::deserialize(bytes.span(), value, context).code, xasrl::error_code::invalid_enum_value);
        EXPECT_EQ(value, ordinary::write);
    }

    TEST(WireContract, RejectsUnclaimedContentAndPreservesTarget) {
        const xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(record{permissions::write}, bytes, context));
        const auto document = srl_test::open_document(bytes.span());
        srl_test::set_u64(bytes, document.inline_definition_offset() + xasrl::detail::definition_header_size + 8, 0);
        record target{permissions::read};
        const auto status = xasrl::deserialize(bytes.span(), target, context);
        EXPECT_EQ(status.code, xasrl::error_code::invalid_payload_shape);
        EXPECT_EQ(target.value, permissions::read);

        auto check_empty = [&]<typename T>(T target) {
            ASSERT_FALSE(xasrl::serialize(target, bytes, context));
            const auto doc = srl_test::open_document(bytes.span());
            const auto content_end = bytes.size();
            bytes.push_back(std::byte{0});
            srl_test::set_u64(bytes, doc.root_offset(), bytes.size() - doc.root_offset());
            const auto parsed = xasrl::detail::validate_document(bytes.span());
            EXPECT_EQ(parsed.code, xasrl::error_code::trailing_bytes);
            EXPECT_EQ(parsed.offset, content_end);
            EXPECT_EQ(xasrl::deserialize(bytes.span(), target, context).code, xasrl::error_code::trailing_bytes);
        };
        check_empty(empty_record{});
        check_empty(std::vector<int>{});
        check_empty(std::map<int, int>{});



        ASSERT_FALSE(xasrl::serialize(std::vector<std::array<int, 0>>{{}, {}}, bytes, context));
        std::vector<std::array<int, 0>> restored;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), restored, context));
        EXPECT_EQ(restored.size(), 2u);
    }

    TEST(Diagnostics, FormatsOptionalIdPathAndSystemError) {
        EXPECT_EQ(xasrl::format_error({}), "none");
        const xasrl::error failure{.code = xasrl::error_code::invalid_bool, .id = 0, .offset = 123};
        EXPECT_EQ(xasrl::format_error(failure, "profile.active"),
            "invalid_bool path=profile.active id=0 offset=123");
        EXPECT_EQ(xasrl::format_error({.code = xasrl::error_code::truncated_entry_header}),
            "truncated_entry_header offset=0");
        const std::error_code system{-42, std::generic_category()};
        const auto text = xasrl::format_error({.code = xasrl::error_code::io_open_failed, .system_error = system});
        EXPECT_NE(text.find(system.category().name()), std::string::npos);
        EXPECT_NE(text.find(":-42 ("), std::string::npos);
        EXPECT_NE(text.find(system.message()), std::string::npos);
    }
}
