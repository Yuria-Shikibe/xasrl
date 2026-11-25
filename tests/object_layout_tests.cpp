#include "test_support.hpp"

#include <cstring>

namespace {
    enum class mode : std::uint8_t { idle, active };
    struct [[=xasrl::tag::leaf{}]] values {
        bool enabled{};
        mode state{};
        mode states[2]{};
    };
    struct [[=xasrl::tag::leaf{.allow_non_unique_presentation = true}]] padded {
        bool enabled{};
        std::uint32_t count{};
    };
    struct [[=xasrl::tag::leaf{.allow_non_unique_presentation = true}]] segmented {
        bool enabled{};
        [[=xasrl::tag::transient{}]] std::uint32_t cache = 99;
        mode state{};
    };
    struct forced {
        [[=xasrl::tag::field{0}, =xasrl::tag::leaf{}]] values data;
    };

    template<typename T>
    void check_generated_validation() {
        constexpr auto schema = xasrl::schema(^^T).tree;
        constexpr auto node = schema.value_index();
        constexpr auto layout = schema[node].leaf().layout();
        static_assert(!layout.fields().empty());
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(T{}, bytes, context));
        template for (constexpr auto field: layout.fields()) {
            using type = typename[:field.type:];
            if constexpr (std::same_as<type, bool> || std::is_enum_v<type>) {
                auto malformed = bytes;
                constexpr auto position = xasrl::detail::document_header_size + xasrl::detail::entry_head_wire_size + field.wire_offset;
                malformed[position] = std::byte{0xff};
                T target{};
                const auto status = xasrl::deserialize(malformed.span(), target, context);
                EXPECT_EQ(status.code, (std::same_as<type, bool>
                    ? xasrl::error_code::invalid_bool : xasrl::error_code::invalid_enum_value));
                EXPECT_EQ(status.offset, position);
                xasrl::byte_buffer restored;
                ASSERT_FALSE(xasrl::serialize(target, restored, context));
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
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(forced{}, bytes, context));
        constexpr auto tree = xasrl::schema(^^forced).tree;
        constexpr auto layout = tree[tree.value_index(), 0].leaf().layout();
        static_assert(layout.fields().size() == 3);
        static_assert(layout.fields()[2].element_count == 2);
        const auto entry = srl_test::open_document(bytes.span());
        constexpr auto state_offset = layout.fields()[1].wire_offset;
        const auto position = entry.at(0).offset + state_offset;
        bytes[position] = std::byte{0xff};
        forced out;
        const auto status = xasrl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(status.code, xasrl::error_code::invalid_enum_value);
        EXPECT_EQ(status.offset, position);
        EXPECT_EQ(status.id, 0u);
    }

    TEST(ObjectLayout, PaddingIsCanonicalAndTransientStateIsPreserved) {
        padded first, second;
        std::memset(&first, 0x11, sizeof(first));
        std::memset(&second, 0x22, sizeof(second));
        first.enabled = second.enabled = true;
        first.count = second.count = 42;
        xasrl::serialization_context context;
        xasrl::byte_buffer left, right;
        ASSERT_FALSE(xasrl::serialize(first, left, context));
        ASSERT_FALSE(xasrl::serialize(second, right, context));
        EXPECT_EQ(left, right);

        segmented source{true, 1, mode::active};
        ASSERT_FALSE(xasrl::serialize(source, left, context));
        segmented out;
        ASSERT_FALSE(xasrl::deserialize(left.span(), out, context));
        EXPECT_EQ(out.cache, 99u);
        EXPECT_EQ(out.state, mode::active);
        EXPECT_TRUE(out.enabled);
    }
}
