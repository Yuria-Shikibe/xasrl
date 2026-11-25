#include "test_support.hpp"

#include <bit>

namespace srl_test {
    template<srl::szt Extent>
    struct [[=srl::tag::leaf{}]] SizingLeaf {};
}

namespace srl {
    // Sizing-only codec: these tests never form payload spans or invoke encoding.
    template<szt Extent>
    struct leaf_codec<srl_test::SizingLeaf<Extent>> {
        static constexpr szt fixed_size = Extent;
        static error encode(const srl_test::SizingLeaf<Extent> &, std::span<std::byte, Extent>);
        static error decode(std::span<const std::byte, Extent>, srl_test::SizingLeaf<Extent> &);
    };
}

namespace srl_test {
    constexpr srl::szt large_extent = (srl::szt{1} << 32) + 7;
    using LargeLeaf = SizingLeaf<large_extent>;

    struct [[=srl::tag::auto_seq{}]] LargeRecord {
        LargeLeaf large;
        std::vector<int> dynamic;
    };

    struct [[=srl::tag::auto_stream{}]] LargeStream {
        LargeLeaf large;
        std::vector<int> dynamic;
    };

    static_assert(srl::schema_valid(^^LargeLeaf));
    constexpr srl::schema large_leaf_schema(^^LargeLeaf);
    static_assert(large_leaf_schema.root_node().kind() == srl::schema_kind::root);
    static_assert(large_leaf_schema.root_node().encode_extent() == srl::schema_extent::fixed(large_extent + 16));
    static_assert(large_leaf_schema.value_node().encode_extent() == srl::schema_extent::fixed(large_extent));
    static_assert(!srl::schema_valid(^^SizingLeaf<srl::detail::maximum_entry_size - 8>));
    static_assert(srl::schema_valid(^^std::array<int, srl::entry_metadata::max_mapping_count>));
    static_assert(!srl::schema_valid(^^std::array<int, srl::entry_metadata::max_mapping_count + 1>));
    static_assert((srl::schema_extent::fixed(large_extent) + srl::schema_extent::fixed(16))
        == srl::schema_extent::fixed(large_extent + 16));
    static_assert(srl::schema_extent::fixed(large_extent).scaled_by(2)
        == srl::schema_extent::fixed(2 * large_extent));
    static_assert(std::bit_cast<std::uint32_t>(srl::entry_metadata{0xffffff, srl::entry_kind::map})
                  == 0x05ffffffu);
    static_assert(srl::entry_metadata{0xffffff, srl::entry_kind::record}.mapping_count() == 0xffffff);
    static_assert(srl::entry_metadata{0, srl::entry_kind::stream}.kind() == srl::entry_kind::stream);

    TEST(LargeEntries, SizePlanningBeyondFourGiB) {
        EXPECT_EQ(srl::serialized_size(LargeLeaf{}, default_context), large_extent + 16);
        EXPECT_EQ(*srl::serialized_size(std::vector<LargeLeaf>(2), default_context),
                  16 + 2 * (16 + large_extent));
        EXPECT_EQ(*srl::serialized_size(LargeRecord{}, default_context), 16 + 32 + large_extent + 16);
        EXPECT_EQ(*srl::serialized_size(LargeStream{}, default_context), 16 + large_extent + 16);

        constexpr std::array<srl::szt, 1> large_slot{large_extent};
        const auto size = srl::detail::measure_fixed_slots(1, large_slot);
        ASSERT_TRUE(size);
        EXPECT_EQ(*size, large_extent + 32);
        constexpr std::array<srl::szt, 1> overflowing_slot{srl::detail::maximum_entry_size};
        EXPECT_EQ(srl::detail::measure_fixed_slots(1, overflowing_slot).error().code,
                  srl::error_code::entry_too_large);
    }

    TEST(LargeEntries, MappingCountBoundary) {
        EXPECT_THROW((srl::entry_metadata{0x1000000u, srl::entry_kind::record}), std::out_of_range);
        EXPECT_THROW((srl::entry_metadata{srl::szt{1} << 32, srl::entry_kind::record}), std::out_of_range);
        constexpr std::array<srl::szt, 1> slot{sizeof(int)};
        constexpr auto maximum = srl::entry_metadata::max_mapping_count;
        const auto boundary = srl::detail::measure_fixed_slots(maximum, slot);
        ASSERT_TRUE(boundary);
        EXPECT_EQ(*boundary, 16 + static_cast<srl::szt>(maximum) * 20);
        const auto overflow = srl::detail::measure_fixed_slots(maximum + 1, slot);
        ASSERT_FALSE(overflow);
        EXPECT_EQ(overflow.error().code, srl::error_code::too_many_mappings);
        EXPECT_EQ(overflow.error().id, maximum);
        constexpr std::array<srl::szt, 2> map_slots{4, 4};
        const auto odd_mapping = srl::detail::measure_fixed_slots(maximum / 2 + 1, map_slots);
        ASSERT_FALSE(odd_mapping);
        EXPECT_EQ(odd_mapping.error().id, maximum);
    }

    TEST(LargeEntries, FullWidthWireValuesAndErrors) {
        srl::byte_buffer bytes;
        bytes.resize(16);
        srl::detail::write_trivial(bytes.span(), srl::entry_head{
            .size = large_extent, .metadata = {0, srl::entry_kind::leaf}, .wire_type = 0x12345678u,
        });
        EXPECT_EQ(get_u64(bytes, 0), large_extent);
        EXPECT_EQ(bytes[4], std::byte{1});
        EXPECT_EQ(bytes[11], std::byte{1});
        EXPECT_EQ(get_u32(bytes, 12), 0x12345678u);
        EXPECT_EQ(srl::detail::parse_entry(bytes.span()).error().code, srl::error_code::invalid_entry_size);

        srl::detail::write_trivial(bytes.span(), srl::entry_mapping{.id = 42, .offset = large_extent});
        EXPECT_EQ(get_u64(bytes, 8), large_extent);
        EXPECT_EQ(get_u32(bytes, 4), 0u);

        srl::byte_buffer pair{};
        const auto pair_status = srl::serialize(Pair{}, pair, default_context);
        ASSERT_TRUE(!pair_status);
        set_u64(pair, 40, (srl::szt{1} << 32) + 4);
        auto parsed = srl::detail::parse_entry(pair.span());
        ASSERT_FALSE(parsed);
        EXPECT_EQ(parsed.error().code, srl::error_code::payload_out_of_bounds);
        EXPECT_EQ(parsed.error().offset, 40u);

        auto error = srl::detail::fail(srl::error_code::invalid_payload_shape, 1, large_extent);
        EXPECT_EQ(error.offset, large_extent);
        EXPECT_EQ(srl::detail::locate_error(error, 2, 16).offset, large_extent + 16);
        error.offset = 0;
        EXPECT_EQ(srl::detail::locate_error(error, 2, large_extent).offset, large_extent);
    }

    TEST(WireFormat, ClassificationAndTableValidation) {
        for (const auto kind : {srl::entry_kind::leaf, srl::entry_kind::stream, srl::entry_kind::record,
                                srl::entry_kind::sequence, srl::entry_kind::map}) {
            srl::byte_buffer bytes;
            bytes.resize(16);
            srl::detail::write_trivial(bytes.span(), srl::entry_head{.size = 16, .metadata = {0, kind}});
            auto parsed = srl::detail::parse_entry(bytes.span());
            ASSERT_TRUE(parsed);
            EXPECT_EQ(parsed->head().kind(), kind);
            EXPECT_EQ(parsed->head().mapping_count(), 0u);
        }
        srl::byte_buffer pair{};
        const auto pair_status = srl::serialize(Pair{}, pair, default_context);
        ASSERT_TRUE(!pair_status);
        for (const unsigned kind : {0u, 6u, 255u, 1u, 2u}) {
            auto invalid = pair;
            invalid[11] = static_cast<std::byte>(kind);
            EXPECT_EQ(srl::detail::parse_entry(invalid.span()).error().code,
                      srl::error_code::invalid_entry_classification);
        }
        auto truncated = pair;
        truncated[8] = std::byte{3};
        EXPECT_EQ(srl::detail::parse_entry(truncated.span()).error().code, srl::error_code::truncated_mapping_table);
        EXPECT_EQ(srl::detail::parse_entry(pair.span().first(15)).error().code,
                  srl::error_code::truncated_entry_header);
        auto reversed = pair;
        set_u64(reversed, 24, 4);
        set_u64(reversed, 40, 0);
        EXPECT_EQ(srl::detail::parse_entry(reversed.span()).error().code, srl::error_code::invalid_mapping_order);
    }
}
