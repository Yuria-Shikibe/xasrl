#include "srl/serialization.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace serialization_api_tests {
    inline const xasrl::serialization_context context;

    struct Record {
        [[=xasrl::tag::field{1}]] int first{};
        [[=xasrl::tag::field{2}]] int second{};
        int runtime_state = 73;
    };

    struct Subset {
        [[=xasrl::tag::field{1}]] int first{};
    };

    struct [[=xasrl::tag::auto_stream{}]] Stream {
        int first{};
        int second{};
    };

    struct MoveOnly {
        [[=xasrl::tag::field{1}]] int value{};
        std::unique_ptr<int> runtime_state;
    };

    struct ThrowingSwap {
        [[=xasrl::tag::field{1}]] int value{};
        friend void swap(ThrowingSwap &, ThrowingSwap &) noexcept(false);
    };

    struct FixedHandlerKey {};
    struct FixedConversion {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^int, xasrl::handler_key<FixedHandlerKey>()}]]
        int value{};
    };

    struct [[=xasrl::tag::leaf{}]] FallibleLeaf {
        int value{};
    };
}

namespace xasrl {
    template<>
    struct leaf_codec<serialization_api_tests::FallibleLeaf> {
        static constexpr szt fixed_size = sizeof(int);
        static inline bool fail_encode{};
        static inline bool throw_encode{};
        static inline bool fail_decode{};
        static inline bool throw_decode{};

        static error encode(const serialization_api_tests::FallibleLeaf &value,
                            std::span<std::byte, fixed_size> bytes) {
            detail::write_trivial(bytes, value.value);
            if (throw_encode) { throw std::runtime_error("encode failed"); }
            return fail_encode ? detail::fail(error_code::invalid_reference) : error{};
        }

        static error decode(std::span<const std::byte, fixed_size> bytes,
                            serialization_api_tests::FallibleLeaf &out) {
            out.value = detail::read_trivial<int>(bytes);
            if (throw_decode) { throw std::runtime_error("decode failed"); }
            return fail_decode ? detail::fail(error_code::invalid_reference) : error{};
        }
    };
}

namespace serialization_api_tests {
    template<typename T>
    concept has_static_size = requires { xasrl::serialized_size<T>(); };

    template<typename T, std::size_t Extent>
    concept serializes_to = requires(const T &value, std::span<std::byte, Extent> bytes) {
        { xasrl::serialize(value, bytes, context) } -> xasrl::codec_status;
    };

    template<typename T, typename Byte = const std::byte, std::size_t Extent = std::dynamic_extent>
    concept deserializes_to = requires(T &out, std::span<Byte, Extent> bytes) {
        { xasrl::deserialize(bytes, out, context) } -> std::same_as<xasrl::error>;
    };

    template<typename T>
    concept deserializes_without_context = requires(T &out, std::span<const std::byte> bytes) {
        xasrl::deserialize(bytes, out);
    };

    static_assert(xasrl::serialized_size<int>() == 16 + xasrl::detail::entry_head_wire_size + sizeof(int));
    static_assert(xasrl::serialized_size<Record>() == 16 + 16 + 4 + 8 + 2 * 16 + 2 * sizeof(int));
    static_assert(xasrl::serialized_size(Stream{}, context) == 16 + 16 + 4 + 8 + 2 * 16 + 2 * sizeof(int));
    static_assert(xasrl::serialized_size<std::array<int, 2>>() == xasrl::serialized_size<int[2]>());
    static_assert(xasrl::serialized_size(FixedConversion{}, context) == xasrl::serialized_size<Subset>());
    static_assert(std::same_as<decltype(xasrl::serialized_size<Record>()), xasrl::szt>);
    static_assert(std::same_as<decltype(xasrl::serialized_size(Record{}, context)), xasrl::szt>);
    static_assert(std::same_as<decltype(xasrl::serialized_size(std::declval<const std::string &>(), context)),
                               std::expected<xasrl::szt, xasrl::error>>);
    static_assert(noexcept(xasrl::serialized_size<Record>()));
    static_assert(noexcept(xasrl::serialized_size(Record{}, context)));
    static_assert(!noexcept(xasrl::serialized_size(std::declval<const std::string &>(), context)));
    static_assert(!has_static_size<std::string>);
    static_assert(serializes_to<Record, xasrl::serialized_size<Record>()>);
    static_assert(!serializes_to<Record, xasrl::serialized_size<Record>() - 1>);
    static_assert(!serializes_to<Record, xasrl::serialized_size<Record>() + 1>);
    static_assert(serializes_to<Record, std::dynamic_extent>);
    static_assert(serializes_to<std::string, 128>);
    static_assert(deserializes_to<Record, std::byte, xasrl::serialized_size<Record>()>);
    static_assert(deserializes_to<Record, const std::byte, xasrl::serialized_size<Record>()>);
    static_assert(deserializes_to<Subset, const std::byte, xasrl::serialized_size<Record>()>);
    static_assert(deserializes_to<int[2], std::byte, xasrl::serialized_size<int[2]>()>);
    static_assert(!deserializes_to<const Record>);
    static_assert(!deserializes_to<volatile Record>);
    static_assert(!deserializes_to<const int[2]>);
    static_assert(!deserializes_to<MoveOnly>);
    static_assert(!deserializes_to<ThrowingSwap>);
    static_assert(!deserializes_without_context<Record>);

    TEST(SerializationApi, StaticSpansAndCompatibleRecord) {
        std::array<std::byte, xasrl::serialized_size<Record>()> bytes{};
        ASSERT_FALSE(xasrl::serialize(Record{12, 34, 999}, std::span{bytes}, context));
        Record out{0, 0, 321};
        ASSERT_FALSE(xasrl::deserialize(std::span{bytes}, out, context));
        EXPECT_EQ(out.first, 12);
        EXPECT_EQ(out.second, 34);
        EXPECT_EQ(out.runtime_state, 321);

        Subset subset{};
        ASSERT_FALSE(xasrl::deserialize(std::span{std::as_const(bytes)}, subset, context));
        EXPECT_EQ(subset.first, 12);

        xasrl::byte_buffer dynamic;
        ASSERT_FALSE(xasrl::serialize(Record{12, 34}, dynamic, context));
        EXPECT_TRUE(std::ranges::equal(bytes, dynamic.span()));
    }

    TEST(SerializationApi, BufferReuseAndRuntimeSpanBounds) {
        xasrl::byte_buffer bytes;
        bytes.reserve(4096);
        bytes.resize(4096);
        const auto pointer = bytes.data();
        const auto capacity = bytes.capacity();
        ASSERT_FALSE(xasrl::serialize(Record{1, 2}, bytes, context));
        EXPECT_EQ(bytes.size(), xasrl::serialized_size<Record>());
        EXPECT_EQ(bytes.data(), pointer);
        EXPECT_EQ(bytes.capacity(), capacity);
        ASSERT_FALSE(xasrl::serialize(42, bytes, context));
        EXPECT_EQ(bytes.size(), xasrl::serialized_size<int>());
        EXPECT_EQ(bytes.data(), pointer);

        std::array<std::byte, xasrl::serialized_size<Record>() + 1> wrong{};
        wrong.fill(std::byte{0x5a});
        const auto original = wrong;
        EXPECT_EQ(xasrl::serialize(Record{}, std::span<std::byte>{wrong}, context).code,
                  xasrl::error_code::invalid_entry_size);
        EXPECT_EQ(xasrl::serialize(Record{}, std::span<std::byte>{wrong}.first(1), context).code,
                  xasrl::error_code::invalid_entry_size);
        EXPECT_EQ(wrong, original);

        const std::string text = "hello";
        const auto size = xasrl::serialized_size(text, context);
        ASSERT_TRUE(size);
        constexpr auto text_extent = 60 + 5 * sizeof(char);
        std::array<std::byte, text_extent> fixed{};
        EXPECT_EQ(*size, fixed.size());
        ASSERT_FALSE(xasrl::serialize(text, std::span{fixed}, context));
        std::string restored = "previous contents";
        ASSERT_FALSE(xasrl::deserialize(std::span{fixed}, restored, context));
        EXPECT_EQ(restored, text);
        const auto prior = fixed;
        EXPECT_EQ(xasrl::serialize(std::string{"longer"}, std::span{fixed}, context).code,
                  xasrl::error_code::invalid_entry_size);
        EXPECT_EQ(fixed, prior);
    }

    TEST(SerializationApi, FixedSizeDoesNotBindHandlers) {
        FixedConversion value{42};
        EXPECT_EQ(xasrl::serialized_size(value, context), xasrl::serialized_size<FixedConversion>());
        xasrl::byte_buffer bytes;
        EXPECT_THROW((void)xasrl::serialize(value, bytes, context), std::logic_error);
    }

    TEST(SerializationApi, NativeArrayCommitAndRollback) {
        int source[]{4, 5};
        int target[]{8, 9};
        std::array<std::byte, xasrl::serialized_size<int[2]>()> bytes{};
        ASSERT_FALSE(xasrl::serialize(source, std::span{bytes}, context));
        EXPECT_EQ(xasrl::deserialize(std::span{bytes}.first(bytes.size() - 1), target, context).code,
                  xasrl::error_code::invalid_entry_size);
        EXPECT_EQ(target[0], 8);
        EXPECT_EQ(target[1], 9);
        ASSERT_FALSE(xasrl::deserialize(std::span{std::as_const(bytes)}, target, context));
        EXPECT_TRUE(std::ranges::equal(source, target));
    }

    TEST(SerializationApi, FailureAndExceptionContracts) {
        using codec = xasrl::leaf_codec<FallibleLeaf>;
        xasrl::byte_buffer bytes;
        bytes.resize(100, std::byte{0x5a});
        codec::fail_encode = true;
        EXPECT_EQ(xasrl::serialize(FallibleLeaf{42}, bytes, context).code,
                  xasrl::error_code::invalid_reference);
        codec::fail_encode = false;
        EXPECT_EQ(bytes.size(), xasrl::serialized_size<FallibleLeaf>());
        codec::throw_encode = true;
        EXPECT_THROW((void)xasrl::serialize(FallibleLeaf{43}, bytes, context), std::runtime_error);
        codec::throw_encode = false;
        ASSERT_FALSE(xasrl::serialize(FallibleLeaf{44}, bytes, context));

        std::array<std::byte, xasrl::serialized_size<FallibleLeaf>()> fixed{};
        codec::fail_encode = true;
        EXPECT_EQ(xasrl::serialize(FallibleLeaf{45}, std::span{fixed}, context).code,
                  xasrl::error_code::invalid_reference);
        codec::fail_encode = false;
        codec::throw_encode = true;
        EXPECT_THROW((void)xasrl::serialize(FallibleLeaf{46}, std::span{fixed}, context), std::runtime_error);
        codec::throw_encode = false;

        FallibleLeaf out{99};
        codec::fail_decode = true;
        EXPECT_EQ(xasrl::deserialize(bytes.span(), out, context).code, xasrl::error_code::invalid_reference);
        codec::fail_decode = false;
        EXPECT_EQ(out.value, 99);
        codec::throw_decode = true;
        EXPECT_THROW((void)xasrl::deserialize(bytes.span(), out, context), std::runtime_error);
        codec::throw_decode = false;
        EXPECT_EQ(out.value, 99);
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out.value, 44);
    }
}
