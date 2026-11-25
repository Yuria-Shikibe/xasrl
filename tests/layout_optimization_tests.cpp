#include "srl/serialization.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <meta>
#include <span>

namespace {
    struct [[=srl::tag::leaf{}]] large_bytes {
        unsigned char bytes[4096]{};
    };

    constexpr srl::schema large_schema(^^large_bytes);
    constexpr auto large_layout = large_schema.value_node().leaf().layout();
    static_assert(large_layout.fields().size() == 1);
    static_assert(large_layout.fields()[0].element_count == 4096);
    static_assert(large_layout.fields()[0].children().size() == 1);

    enum class state : std::uint8_t { idle, active };
    struct [[=srl::tag::leaf{}]] nested_values {
        state values[2][3]{};
    };
    struct item {
        bool enabled{};
        [[=srl::tag::transient{}]] std::uint32_t cache{99};
        state value{};
    };
    struct [[=srl::tag::leaf{.allow_non_unique_presentation = true}]] padded_array {
        item values[3]{};
        [[=srl::tag::transient{}]] std::uint32_t cache{101};
        std::uint32_t tail{};
    };

    // Reference the original v4 scalar traversal independently of compressed
    // metadata: array nesting must not change scalar order or absolute offsets.
    consteval srl::type_hash_type flat_hash(std::meta::info type, srl::type_hash_type hash,
                                           srl::szt offset, std::meta::info entity = {},
                                           std::span<const srl::detail::byte_range> ranges = {}) {
        type = std::meta::remove_cv(type);
        if (std::meta::is_array_type(type)) {
            const auto element = std::meta::remove_extent(type);
            for (srl::szt i = 0; i != std::meta::extent(type); ++i) {
                hash = flat_hash(element, hash, offset + i * std::meta::size_of(element), entity, ranges);
            }
        } else if (std::meta::is_class_type(type)) {
            for (auto child: srl::detail::reflected_entities(type)) {
                if (srl::detail::annotation_count(child, ^^srl::tag::transient)) { continue; }
                hash = flat_hash(std::meta::type_of(child), hash,
                    offset + std::meta::offset_of(child).bytes, child, ranges);
            }
        } else {
            hash = srl::detail::hash_integer(hash, srl::detail::scalar_wire_type(type));
            srl::szt preceding{};
            for (const auto range: ranges) {
                if (offset >= range.offset && offset + std::meta::size_of(type) <= range.offset + range.size) {
                    offset = preceding + offset - range.offset;
                    break;
                }
                preceding += range.size;
            }
            hash = srl::detail::hash_integer(hash, offset);
            hash = srl::detail::hash_integer(hash, std::meta::size_of(type));
            if (entity != std::meta::info{} && !std::meta::is_base(entity)) {
                hash = srl::detail::hash_string(hash, std::meta::identifier_of(entity));
            }
        }
        return hash;
    }

    constexpr srl::schema nested_schema(^^nested_values);
    static_assert(nested_schema.value_node().wire_type()
        == flat_hash(^^nested_values,
            srl::detail::hash_integer(srl::detail::scalar_wire_type(^^nested_values), sizeof(nested_values)), 0));
    constexpr srl::schema padded_schema(^^padded_array);
    static_assert(padded_schema.value_node().wire_type()
        == flat_hash(^^padded_array,
            srl::detail::hash_integer(srl::detail::scalar_wire_type(^^padded_array),
                srl::leaf_codec<padded_array>::fixed_size), 0, {},
            srl::detail::leaf_copy_ranges(^^padded_array)));

    TEST(LayoutOptimization, LargeByteArrayUsesOneRepeatedLayoutAndRoundTrips) {
        large_bytes source, result;
        source.bytes[0] = 42;
        source.bytes[4095] = 99;
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(source, bytes, context));
        EXPECT_EQ(bytes.size(), 32 + sizeof(source));
        ASSERT_FALSE(srl::deserialize(bytes.span(), result, context));
        EXPECT_EQ(std::memcmp(source.bytes, result.bytes, sizeof(source.bytes)), 0);
    }

    TEST(LayoutOptimization, NestedArrayChecksEveryElementWithAbsoluteOffsets) {
        nested_values source;
        source.values[1][2] = state::active;
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(source, bytes, context));
        for (std::size_t i = 0; i != 6; ++i) {
            auto malformed = bytes;
            malformed[32 + i] = std::byte{0xff};
            nested_values result = source;
            const auto status = srl::deserialize(malformed.span(), result, context);
            EXPECT_EQ(status.code, srl::error_code::invalid_enum_value);
            EXPECT_EQ(status.offset, 32 + i);
            EXPECT_EQ(std::memcmp(&result, &source, sizeof(source)), 0);
        }
    }

    TEST(LayoutOptimization, RepeatedPaddedObjectsZeroPaddingAndPreserveTransientState) {
        padded_array first{}, second{};
        std::memset(&first, 0x11, sizeof(first));
        std::memset(&second, 0x22, sizeof(second));
        for (std::size_t i = 0; i != 3; ++i) {
            first.values[i].enabled = second.values[i].enabled = i != 0;
            first.values[i].value = second.values[i].value = state::active;
        }
        first.tail = second.tail = 7;
        srl::serialization_context context;
        srl::byte_buffer left, right;
        ASSERT_FALSE(srl::serialize(first, left, context));
        ASSERT_FALSE(srl::serialize(second, right, context));
        EXPECT_EQ(left, right);
        padded_array result;
        ASSERT_FALSE(srl::deserialize(left.span(), result, context));
        EXPECT_EQ(result.cache, 101u);
        EXPECT_EQ(result.tail, 7u);
        for (std::size_t i = 0; i != 3; ++i) {
            EXPECT_EQ(result.values[i].cache, 99u);
            EXPECT_EQ(result.values[i].enabled, i != 0);
            EXPECT_EQ(result.values[i].value, state::active);
        }
        constexpr srl::schema schema(^^padded_array);
        constexpr auto group = schema.value_node().leaf().layout().fields()[0];
        constexpr auto state_offset = group.children()[1].wire_offset;
        constexpr auto position = 32 + group.wire_offset + 2 * group.stride + state_offset;
        left[position] = std::byte{0xff};
        const auto status = srl::deserialize(left.span(), result, context);
        EXPECT_EQ(status.code, srl::error_code::invalid_enum_value);
        EXPECT_EQ(status.offset, position);
        EXPECT_EQ(result.values[2].cache, 99u);
        EXPECT_EQ(result.values[2].value, state::active);
        constexpr auto bool_position = 32 + group.wire_offset + group.stride;
        right[bool_position] = std::byte{2};
        const auto invalid_bool = srl::deserialize(right.span(), result, context);
        EXPECT_EQ(invalid_bool.code, srl::error_code::invalid_bool);
        EXPECT_EQ(invalid_bool.offset, bool_position);
        EXPECT_TRUE(result.values[1].enabled);
    }
}
