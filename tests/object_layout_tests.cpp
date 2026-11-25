#include "test_support.hpp"

#include <cstring>

namespace {
    enum class mode : std::uint8_t { idle, active };
    struct [[=srl::tag::leaf{}]] values {
        bool enabled{};
        mode state{};
        mode states[2]{};
    };
    struct [[=srl::tag::leaf{.allow_non_unique_presentation = true}]] padded {
        bool enabled{};
        std::uint32_t count{};
    };
    struct [[=srl::tag::leaf{.allow_non_unique_presentation = true}]] segmented {
        bool enabled{};
        [[=srl::tag::transient{}]] std::uint32_t cache = 99;
        mode state{};
    };
    struct forced {
        [[=srl::tag::field{0}, =srl::tag::leaf{}]] values data;
    };

    template<typename T>
    void check_generated_validation() {
        constexpr auto schema = srl::schema(^^T).tree;
        constexpr auto node = schema.value_index();
        constexpr auto layout = schema[node].leaf().layout().value();
        static_assert(!layout.fields().empty());
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(T{}, bytes, context));
        template for (constexpr auto field: layout.fields()) {
            using type = typename[:field.type:];
            if constexpr (std::same_as<type, bool> || std::is_enum_v<type>) {
                auto malformed = bytes;
                constexpr auto position = srl::detail::entry_head_wire_size + field.wire_offset;
                malformed[position] = std::byte{0xff};
                T target{};
                const auto status = srl::deserialize(malformed.span(), target, context);
                EXPECT_EQ(status.code, (std::same_as<type, bool>
                    ? srl::error_code::invalid_bool : srl::error_code::invalid_enum_value));
                EXPECT_EQ(status.offset, position);
                srl::byte_buffer restored;
                ASSERT_FALSE(srl::serialize(target, restored, context));
                EXPECT_EQ(restored, bytes);
            }
        }
    }

    TEST(ObjectLayout, SchemaDrivesMalformedScalarCases) {
        check_generated_validation<values>();
        check_generated_validation<padded>();
        check_generated_validation<segmented>();
    }

    TEST(ObjectLayout, ForcedLeafUsesTheSameValidation) {
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(forced{}, bytes, context));
        constexpr auto tree = srl::schema(^^forced).tree;
        constexpr auto layout = tree[tree.value_index(), 0].leaf().layout().value();
        static_assert(layout.fields().size() == 3);
        static_assert(layout.fields()[2].element_count == 2);
        const auto entry = srl::detail::parse_entry(bytes.span());
        ASSERT_TRUE(entry);
        constexpr auto state_offset = layout.fields()[1].wire_offset;
        const auto position = entry->at(0).offset + state_offset;
        bytes[position] = std::byte{0xff};
        forced out;
        const auto status = srl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(status.code, srl::error_code::invalid_enum_value);
        EXPECT_EQ(status.offset, position);
        EXPECT_EQ(status.id, 0u);
    }

    TEST(ObjectLayout, PaddingIsCanonicalAndTransientStateIsPreserved) {
        padded first, second;
        std::memset(&first, 0x11, sizeof(first));
        std::memset(&second, 0x22, sizeof(second));
        first.enabled = second.enabled = true;
        first.count = second.count = 42;
        srl::serialization_context context;
        srl::byte_buffer left, right;
        ASSERT_FALSE(srl::serialize(first, left, context));
        ASSERT_FALSE(srl::serialize(second, right, context));
        EXPECT_EQ(left, right);

        segmented source{true, 1, mode::active};
        ASSERT_FALSE(srl::serialize(source, left, context));
        segmented out;
        ASSERT_FALSE(srl::deserialize(left.span(), out, context));
        EXPECT_EQ(out.cache, 99u);
        EXPECT_EQ(out.state, mode::active);
        EXPECT_TRUE(out.enabled);
    }
}
