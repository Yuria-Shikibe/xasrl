#include "test_support.hpp"

#include <bit>

namespace srl_test {
    template<xasrl::szt Extent>
    struct [[=xasrl::tag::leaf{}]] SizingLeaf {};
}

namespace xasrl {
    // Sizing-only codec: these tests never form payload spans or invoke encoding.
    template<szt Extent>
    struct leaf_codec<srl_test::SizingLeaf<Extent>> {
        static constexpr szt fixed_size = Extent;
        static error encode(const srl_test::SizingLeaf<Extent> &, std::span<std::byte, Extent>);
        static error decode(std::span<const std::byte, Extent>, srl_test::SizingLeaf<Extent> &);
    };
}

namespace srl_test {
    constexpr xasrl::szt large_extent = (xasrl::szt{1} << 32) + 7;
    using LargeLeaf = SizingLeaf<large_extent>;

    struct [[=xasrl::tag::auto_seq{}]] LargeRecord {
        LargeLeaf large;
        std::vector<int> dynamic;
    };

    struct [[=xasrl::tag::auto_stream{}]] LargeStream {
        LargeLeaf large;
        std::vector<int> dynamic;
    };

    static_assert(xasrl::schema_valid(^^LargeLeaf));
    constexpr xasrl::schema large_leaf_schema(^^LargeLeaf);
    static_assert(large_leaf_schema.root_node().kind() == xasrl::schema_kind::root);
    static_assert(large_leaf_schema.root_node().encode_extent() == xasrl::schema_extent::fixed(large_extent + 16));
    static_assert(large_leaf_schema.value_node().encode_extent() == xasrl::schema_extent::fixed(large_extent));
    static_assert(!xasrl::schema_valid(^^SizingLeaf<xasrl::detail::maximum_entry_size - 8>));
    static_assert(xasrl::schema_valid(^^std::array<int, xasrl::entry_metadata::max_mapping_count>));
    static_assert(!xasrl::schema_valid(^^std::array<int, xasrl::entry_metadata::max_mapping_count + 1>));
    static_assert((xasrl::schema_extent::fixed(large_extent) + xasrl::schema_extent::fixed(16))
        == xasrl::schema_extent::fixed(large_extent + 16));
    static_assert(xasrl::schema_extent::fixed(large_extent).scaled_by(2)
        == xasrl::schema_extent::fixed(2 * large_extent));
    static_assert(std::bit_cast<std::uint32_t>(xasrl::entry_metadata{0xffffff, xasrl::entry_kind::map})
                  == 0x05ffffffu);
    static_assert(xasrl::entry_metadata{0xffffff, xasrl::entry_kind::record}.mapping_count() == 0xffffff);
    static_assert(xasrl::entry_metadata{0, xasrl::entry_kind::stream}.kind() == xasrl::entry_kind::stream);

    TEST(LargeEntries, SizePlanningBeyondFourGiB) {
        EXPECT_EQ(xasrl::serialized_size(LargeLeaf{}, default_context), large_extent + 32);
        EXPECT_EQ(*xasrl::serialized_size(std::vector<LargeLeaf>(2), default_context),
                  60 + 2 * large_extent);
        EXPECT_EQ(*xasrl::serialized_size(LargeRecord{}, default_context), 76 + large_extent + 44);
        EXPECT_EQ(*xasrl::serialized_size(LargeStream{}, default_context), 76 + large_extent + 44);

        constexpr std::array<xasrl::szt, 1> large_slot{large_extent};
        const auto size = xasrl::detail::measure_fixed_slots(1, large_slot);
        ASSERT_TRUE(size);
        EXPECT_EQ(*size, large_extent + 16);
        constexpr std::array<xasrl::szt, 1> overflowing_slot{xasrl::detail::maximum_entry_size};
        EXPECT_EQ(xasrl::detail::measure_fixed_slots(1, overflowing_slot).error().code,
                  xasrl::error_code::entry_too_large);
    }

    TEST(LargeEntries, MappingCountBoundary) {
        EXPECT_THROW((xasrl::entry_metadata{0x1000000u, xasrl::entry_kind::record}), std::out_of_range);
        EXPECT_THROW((xasrl::entry_metadata{xasrl::szt{1} << 32, xasrl::entry_kind::record}), std::out_of_range);
        constexpr std::array<xasrl::szt, 1> slot{sizeof(int)};
        constexpr auto maximum = xasrl::entry_metadata::max_mapping_count;
        const auto boundary = xasrl::detail::measure_fixed_slots(maximum, slot);
        ASSERT_TRUE(boundary);
        EXPECT_EQ(*boundary, 16 + static_cast<xasrl::szt>(maximum) * 4);
        const auto overflow = xasrl::detail::measure_fixed_slots(maximum + 1, slot);
        ASSERT_FALSE(overflow);
        EXPECT_EQ(overflow.error().code, xasrl::error_code::too_many_mappings);
        EXPECT_EQ(overflow.error().id, maximum);
        constexpr std::array<xasrl::szt, 2> map_slots{4, 4};
        const auto odd_mapping = xasrl::detail::measure_fixed_slots(maximum / 2 + 1, map_slots);
        ASSERT_FALSE(odd_mapping);
        EXPECT_EQ(odd_mapping.error().id, maximum);
    }

    TEST(LargeEntries, FullWidthWireValuesAndErrors) {
        xasrl::byte_buffer bytes;
        bytes.resize(16);
        xasrl::detail::write_trivial(bytes.span(), xasrl::entry_head{
            .size = large_extent, .metadata = {0, xasrl::entry_kind::leaf}, .wire_type = 0x12345678u,
        });
        EXPECT_EQ(get_u64(bytes, 0), large_extent);
        EXPECT_EQ(bytes[4], std::byte{1});
        EXPECT_EQ(bytes[11], std::byte{1});
        EXPECT_EQ(get_u32(bytes, 12), 0x12345678u);
        const auto parsed = xasrl::detail::defined_entry::parse(bytes.span(), {}, {});
        ASSERT_FALSE(parsed);
        EXPECT_EQ(parsed.error().code, xasrl::error_code::invalid_entry_size);

        xasrl::byte_buffer pair;
        ASSERT_FALSE(xasrl::serialize(Pair{}, pair, default_context));
        const auto doc = open_document(pair.span());
        const auto width = doc.inline_definition_offset() + xasrl::detail::definition_header_size + 8;
        const auto first_offset = doc.at(0).offset;
        set_u64(pair, width, (xasrl::szt{1} << 32) + 4);
        Pair target{};
        const auto malformed = xasrl::deserialize(pair.span(), target, default_context);
        EXPECT_EQ(malformed.code, xasrl::error_code::payload_out_of_bounds);
        EXPECT_EQ(malformed.offset, first_offset);
        auto error = xasrl::detail::fail(xasrl::error_code::invalid_payload_shape, 1, large_extent);
        EXPECT_EQ(error.offset, large_extent);
        EXPECT_EQ(xasrl::detail::locate_error(error, 2, 16).offset, large_extent + 16);
        error.offset = 0;
        EXPECT_EQ(xasrl::detail::locate_error(error, 2, large_extent).offset, large_extent);
    }

    TEST(WireFormat, ClassificationAndTableValidation) {
        xasrl::byte_buffer pair{};
        ASSERT_FALSE(xasrl::serialize(Pair{}, pair, default_context));
        const auto doc = open_document(pair.span());
        for (const unsigned kind : {0u, 6u, 255u, 1u, 2u}) {
            auto invalid = pair;
            invalid[doc.root_offset() + 11] = static_cast<std::byte>(kind);
            EXPECT_EQ(xasrl::detail::validate_document(invalid.span()).code,
                      xasrl::error_code::invalid_entry_classification);
        }
        auto truncated = pair;
        set_u32(truncated, doc.inline_definition_offset(), 100);
        EXPECT_EQ(xasrl::detail::validate_document(truncated.span()).code, xasrl::error_code::truncated_mapping_table);
        EXPECT_EQ(xasrl::detail::validate_document(pair.span().first(15)).code,
                  xasrl::error_code::truncated_entry_header);
        auto reversed = pair;
        set_u32(reversed, doc.inline_definition_offset() + xasrl::detail::definition_header_size, 100);
        EXPECT_EQ(xasrl::detail::validate_document(reversed.span()).code, xasrl::error_code::invalid_mapping_order);
    }
}