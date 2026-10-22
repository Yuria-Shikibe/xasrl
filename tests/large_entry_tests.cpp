#include "test_support.hpp"

#include <bit>

namespace srl_test {
    template<srl::szt Extent>
    struct [[=srl::tag::atomic{}]] SizingAtomic {};
}

namespace srl {
    // Sizing-only codec: these tests never form payload spans or invoke encoding.
    template<szt Extent>
    struct atomic_codec<srl_test::SizingAtomic<Extent>> {
        static constexpr szt fixed_size = Extent;
        static error encode(const srl_test::SizingAtomic<Extent> &, std::span<std::byte, Extent>);
        static error decode(std::span<const std::byte, Extent>, srl_test::SizingAtomic<Extent> &);
    };
}

namespace srl_test {
    constexpr srl::szt large_extent = (srl::szt{1} << 32) + 7;
    using LargeAtomic = SizingAtomic<large_extent>;

    struct [[=srl::tag::auto_seq{}]] LargeRecord {
        LargeAtomic large;
        std::vector<int> dynamic;
    };

    struct [[=srl::tag::auto_stream{}]] LargeStream {
        LargeAtomic large;
        std::vector<int> dynamic;
    };

    static_assert(srl::schema_valid<LargeAtomic>());
    static_assert(srl::schema<LargeAtomic>::tree.node(0).encode_extent == large_extent + 16);
    static_assert(!srl::schema_valid<SizingAtomic<srl::detail::maximum_entry_size - 8>>());
    static_assert(srl::schema_valid<std::array<int, srl::entry_metadata::max_mapping_count>>());
    static_assert(!srl::schema_valid<std::array<int, srl::entry_metadata::max_mapping_count + 1>>());
    static_assert([] consteval {
        srl::detail::schema_builder builder;
        return builder.add_size(large_extent, 16) == large_extent + 16
            && builder.multiply_size(2, large_extent) == 2 * large_extent && builder.valid;
    }());
    static_assert([] consteval {
        srl::detail::schema_builder builder;
        builder.add_size(srl::detail::maximum_entry_size, 1);
        return !builder.valid;
    }());
    static_assert([] consteval {
        srl::detail::schema_builder builder;
        builder.multiply_size(2, srl::detail::maximum_entry_size);
        return !builder.valid;
    }());
    static_assert(std::bit_cast<std::uint32_t>(srl::entry_metadata{0xffffff, srl::entry_kind::map})
                  == 0x05ffffffu);
    static_assert(srl::entry_metadata{0xffffff, srl::entry_kind::record}.mapping_count() == 0xffffff);
    static_assert(srl::entry_metadata{0, srl::entry_kind::stream}.kind() == srl::entry_kind::stream);

    TEST(LargeEntries, SizePlanningBeyondFourGiB) {
        EXPECT_EQ(srl::serialized_size(LargeAtomic{}, default_context), large_extent + 16);
        EXPECT_EQ(*srl::serialized_size(std::vector<LargeAtomic>(2), default_context),
                  16 + 2 * (16 + large_extent));
        EXPECT_EQ(*srl::serialized_size(LargeRecord{}, default_context), 16 + 32 + large_extent + 16);
        EXPECT_EQ(*srl::serialized_size(LargeStream{}, default_context), 16 + large_extent + 16);

        using codec = srl::detail::root_codec<std::vector<int>>;
        codec::encode_plan plan;
        plan.payload.size = large_extent;
        ASSERT_FALSE(plan.prepare(std::vector<int>{1}, default_context));
        EXPECT_EQ(plan.size, large_extent + 16 + sizeof(int));

        codec::encode_plan overflow;
        overflow.payload.size = srl::detail::maximum_entry_size;
        EXPECT_EQ(overflow.prepare(std::vector<int>{1}, default_context).code, srl::error_code::entry_too_large);
    }

    TEST(LargeEntries, MappingCountBoundary) {
        EXPECT_THROW((srl::entry_metadata{0x1000000u, srl::entry_kind::record}), std::out_of_range);
        EXPECT_THROW((srl::entry_metadata{srl::szt{1} << 32, srl::entry_kind::record}), std::out_of_range);
        using codec = srl::detail::root_codec<std::vector<int>>;
        codec::encode_plan boundary;
        boundary.payload.slot_count = srl::entry_metadata::max_mapping_count - 1;
        ASSERT_FALSE(boundary.prepare(std::vector<int>{1}, default_context));
        EXPECT_EQ(boundary.payload.slot_count, srl::entry_metadata::max_mapping_count);
        codec::encode_plan overflow;
        overflow.payload.slot_count = srl::entry_metadata::max_mapping_count;
        EXPECT_EQ(overflow.prepare(std::vector<int>{1}, default_context).code, srl::error_code::too_many_mappings);
    }

    TEST(LargeEntries, FullWidthWireValuesAndErrors) {
        srl::byte_buffer bytes;
        bytes.resize(16);
        srl::detail::write_trivial(bytes.span(), srl::entry_head{
            .size = large_extent, .metadata = {0, srl::entry_kind::atomic}, .wire_type = 0x12345678u,
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
        for (const auto kind : {srl::entry_kind::atomic, srl::entry_kind::stream, srl::entry_kind::record,
                                srl::entry_kind::sequence, srl::entry_kind::map}) {
            srl::byte_buffer bytes;
            bytes.resize(16);
            srl::detail::write_trivial(bytes.span(), srl::entry_head{.size = 16, .metadata = {0, kind}});
            auto parsed = srl::detail::parse_entry(bytes.span());
            ASSERT_TRUE(parsed);
            EXPECT_EQ(parsed->head.kind(), kind);
            EXPECT_EQ(parsed->head.mapping_count(), 0u);
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
