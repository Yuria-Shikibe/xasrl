#include <memory_resource>

#include "test_support.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace status_tests {
    using srl_test::default_context;

    template<bool EncodeFails, bool DecodeFails>
    struct [[=xasrl::tag::leaf{}]] CustomLeaf {
        int value{};
        ~CustomLeaf() {}
        bool operator==(const CustomLeaf &) const = default;
    };

    struct [[=xasrl::tag::leaf{}]] InvalidStatus {};

    template<typename T>
    struct [[=xasrl::tag::auto_seq{}]] Record {
        T value{};
    };

    struct [[=xasrl::tag::auto_stream{}]] Stream {
        int value{};
        double weight{};
    };

    struct Forced {
        [[=xasrl::tag::field{1}, =xasrl::tag::leaf{}]] int values[2]{};
    };

    struct [[=xasrl::tag::leaf{}]] BoolLeaf { bool value{}; };
    struct [[=xasrl::tag::leaf{}]] EnumLeaf { activity value{}; };

    struct ConvertedValue {
        int value{};
        static inline int conversions{};
        ConvertedValue() = default;
        explicit ConvertedValue(int value) : value(value) {}
        explicit operator int() const { ++conversions; return value; }
    };

    struct Converted {
        [[=xasrl::tag::field{1}, =xasrl::conversion_tag{^^int}]] ConvertedValue value;
    };

    struct HandlerKey {};
    struct Handled {
        [[=xasrl::tag::field{1}, =xasrl::conversion_tag{^^int, xasrl::handler_key<HandlerKey>()}]] int value{};
    };
}

namespace xasrl {
    template<bool EncodeFails, bool DecodeFails>
    struct leaf_codec<status_tests::CustomLeaf<EncodeFails, DecodeFails>> {
        using value_type = status_tests::CustomLeaf<EncodeFails, DecodeFails>;
        static constexpr szt fixed_size = sizeof(int);
        static inline bool fail{};
        static inline bool throws{};
        static inline int writes{};
        static inline int reads{};

        static detail::status_type<!EncodeFails> encode(
            const value_type &value, std::span<std::byte, fixed_size> bytes) {
            ++writes;
            if (throws) { throw std::runtime_error("custom encode"); }
            if constexpr (EncodeFails) {
                if (fail) { return detail::fail(error_code::invalid_reference, std::nullopt, 1); }
            }
            detail::write_trivial(bytes, value.value);
            return {};
        }

        static detail::status_type<!DecodeFails> decode(
            std::span<const std::byte, fixed_size> bytes, value_type &out) {
            ++reads;
            out.value = detail::read_trivial<int>(bytes);
            if (throws) { throw std::runtime_error("custom decode"); }
            if constexpr (DecodeFails) {
                if (fail) { return detail::fail(error_code::invalid_reference, std::nullopt, 2); }
            }
            return {};
        }
    };

    template<>
    struct leaf_codec<status_tests::InvalidStatus> {
        static constexpr szt fixed_size = 1;
        static bool encode(const status_tests::InvalidStatus &, std::span<std::byte, 1>);
        static bool decode(std::span<const std::byte, 1>, status_tests::InvalidStatus &);
    };
}

namespace status_tests {
    template<typename T>
    using plan = typename xasrl::detail::root_codec<T>::encode_plan;

    template<typename T>
    using leaf_encode_status = decltype(xasrl::leaf_codec<T>::encode(
        std::declval<const T &>(), std::declval<std::span<std::byte, xasrl::leaf_codec<T>::fixed_size>>()));

    template<typename T>
    using leaf_decode_status = decltype(xasrl::leaf_codec<T>::decode(
        std::declval<std::span<const std::byte, xasrl::leaf_codec<T>::fixed_size>>(), std::declval<T &>()));

    template<typename T, std::size_t Extent = xasrl::serialized_size<T>()>
    using serialize_status = decltype(xasrl::serialize(std::declval<const T &>(),
        std::declval<std::span<std::byte, Extent>>(), default_context));

    template<typename T>
    constexpr bool preparation_succeeds = std::same_as<typename plan<T>::prepare_result, xasrl::always_success>;

    template<typename T>
    constexpr bool encoding_succeeds = std::same_as<typename plan<T>::encode_result, xasrl::always_success>;

    using Success = CustomLeaf<false, false>;
    using EncodeFailure = CustomLeaf<true, false>;
    using DecodeFailure = CustomLeaf<false, true>;

    static_assert(std::is_empty_v<xasrl::always_success>);
    static_assert(!xasrl::always_success{});
    static_assert(!xasrl::always_success{}.set_id(7).rebase(42));
    static_assert(std::same_as<decltype(xasrl::always_success{}.set_id(7).rebase(42)), xasrl::always_success &>);
    static_assert(std::convertible_to<xasrl::always_success, xasrl::error>);
    static_assert(!std::constructible_from<xasrl::always_success, xasrl::error>);
    static_assert(xasrl::codec_status<xasrl::error> && xasrl::codec_status<xasrl::always_success>);
    static_assert(!xasrl::codec_status<bool>);
    static_assert(!xasrl::leaf_serializable<InvalidStatus>);
    static_assert(!xasrl::leaf_serializable<DynamicSpanCodec>);
    static_assert(xasrl::leaf_serializable<Success> && xasrl::leaf_serializable<EncodeFailure>);
    static_assert(std::same_as<leaf_encode_status<bool>, xasrl::always_success>);
    static_assert(std::same_as<leaf_encode_status<activity>, xasrl::always_success>);
    static_assert(std::same_as<leaf_decode_status<int>, xasrl::always_success>);
    static_assert(std::same_as<leaf_decode_status<double>, xasrl::always_success>);
    static_assert(std::same_as<leaf_decode_status<bool>, xasrl::error>);
    static_assert(std::same_as<leaf_decode_status<activity>, xasrl::error>);
    static_assert(std::same_as<leaf_decode_status<UniqueLeaf>, xasrl::always_success>);
    static_assert(std::same_as<leaf_decode_status<PaddedLeaf>, xasrl::always_success>);
    static_assert(std::same_as<leaf_decode_status<TransientLeaf>, xasrl::always_success>);
    static_assert(std::same_as<leaf_decode_status<BoolLeaf>, xasrl::error>);
    static_assert(std::same_as<leaf_decode_status<EnumLeaf>, xasrl::error>);
    static_assert(std::same_as<leaf_encode_status<Success>, xasrl::always_success>);
    static_assert(std::same_as<leaf_decode_status<Success>, xasrl::always_success>);
    static_assert(std::same_as<leaf_encode_status<EncodeFailure>, xasrl::error>);
    static_assert(std::same_as<leaf_decode_status<EncodeFailure>, xasrl::always_success>);
    static_assert(std::same_as<leaf_encode_status<DecodeFailure>, xasrl::always_success>);
    static_assert(std::same_as<leaf_decode_status<DecodeFailure>, xasrl::error>);
    static_assert(preparation_succeeds<Record<Record<int>>> && encoding_succeeds<Record<Record<int>>>);
    static_assert(preparation_succeeds<Stream> && encoding_succeeds<Stream>);
    static_assert(preparation_succeeds<Forced> && encoding_succeeds<Forced>);
    static_assert(preparation_succeeds<std::array<int, 3>> && encoding_succeeds<std::array<int, 3>>);
    static_assert(preparation_succeeds<int[3]> && encoding_succeeds<int[3]>);
    static_assert(preparation_succeeds<Converted> && encoding_succeeds<Converted>);
    static_assert(!preparation_succeeds<Handled> && encoding_succeeds<Handled>);
    static_assert(!preparation_succeeds<std::vector<int>> && encoding_succeeds<std::vector<int>>);
    static_assert(!preparation_succeeds<Record<std::string>> && encoding_succeeds<Record<std::string>>);
    static_assert(!encoding_succeeds<Record<EncodeFailure>>);
    static_assert(!encoding_succeeds<std::vector<EncodeFailure>>);
    static_assert(encoding_succeeds<std::map<int, Success>>);
    static_assert(!encoding_succeeds<std::map<int, EncodeFailure>>);
    static_assert(preparation_succeeds<std::array<EncodeFailure, 0>>);
    static_assert(encoding_succeeds<std::array<EncodeFailure, 0>>);
    static_assert(preparation_succeeds<std::array<Handled, 0>>);
    static_assert(std::same_as<serialize_status<Record<int>>, xasrl::always_success>);
    static_assert(std::same_as<serialize_status<Converted>, xasrl::always_success>);
    static_assert(std::same_as<serialize_status<Handled>, xasrl::error>);
    static_assert(std::same_as<serialize_status<Success>, xasrl::always_success>);
    static_assert(std::same_as<serialize_status<EncodeFailure>, xasrl::error>);
    static_assert(std::same_as<serialize_status<int, std::dynamic_extent>, xasrl::error>);
    static_assert(!noexcept(xasrl::leaf_codec<Success>::encode(
        std::declval<const Success &>(), std::declval<std::span<std::byte, sizeof(int)>>())));

    TEST(Status, SuccessConvertsWithoutDiagnosticState) {
        const xasrl::error status = xasrl::always_success{}.set_id(7).rebase(42);
        EXPECT_FALSE(status);
        EXPECT_FALSE(status.id);
        EXPECT_EQ(status.offset, 0u);
        EXPECT_FALSE(status.system_error);
    }

    TEST(Status, StaticAndDynamicPayloadBoundaries) {
        static constexpr xasrl::schema schema(^^int);
        using codec = xasrl::detail::value_codec<schema.tree, schema.tree.value_index()>;
        std::array<std::byte, sizeof(int)> bytes{};
        xasrl::detail::write_trivial(std::span{bytes}, 73);
        int value{};
        auto success = codec::decode(std::span<const std::byte, sizeof(int)>{bytes}, value, default_context);
        static_assert(std::same_as<decltype(success), xasrl::always_success>);
        EXPECT_FALSE(success);
        EXPECT_EQ(value, 73);
        auto dynamic = codec::decode(std::span<const std::byte>{bytes}, value, default_context);
        static_assert(std::same_as<decltype(dynamic), xasrl::error>);
        EXPECT_FALSE(dynamic);
        value = 19;
        const auto truncated = codec::decode(std::span<const std::byte>{bytes}.first(1), value,
                                             default_context);
        EXPECT_EQ(truncated.code, xasrl::error_code::invalid_payload_shape);
        EXPECT_FALSE(truncated.id);
        EXPECT_EQ(truncated.offset, 0u);
        const auto wrong_static = codec::decode(std::span<const std::byte, sizeof(int)>{bytes}.first<1>(),
                                                value, default_context);
        EXPECT_EQ(wrong_static.code, xasrl::error_code::invalid_payload_shape);
        EXPECT_EQ(value, 19);
    }

    TEST(Status, SuccessOperationsStillExecuteAndExceptionsPropagate) {
        using codec = xasrl::leaf_codec<Success>;
        codec::writes = codec::reads = 0;
        const Record<Success> source{{73}};
        std::array<std::byte, xasrl::serialized_size<decltype(source)>()> bytes{};
        EXPECT_FALSE(xasrl::serialize(source, std::span{bytes}, default_context));
        EXPECT_EQ(codec::writes, 1);
        Record<Success> target{{19}};
        EXPECT_FALSE(xasrl::deserialize(std::span{bytes}, target, default_context));
        EXPECT_EQ(codec::reads, 1);
        EXPECT_EQ(target.value.value, 73);
        codec::throws = true;
        EXPECT_THROW((void)xasrl::serialize(source, std::span{bytes}, default_context), std::runtime_error);
        target.value.value = 19;
        EXPECT_THROW((void)xasrl::deserialize(std::span{bytes}, target, default_context), std::runtime_error);
        codec::throws = false;
        EXPECT_EQ(target.value.value, 19);
    }

    TEST(Status, CachedConversionsAndEmptyContainers) {
        xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
        const std::array<Converted, 2> source{{{ConvertedValue{31}}, {ConvertedValue{42}}}};
        ConvertedValue::conversions = 0;
        std::array<std::byte, xasrl::serialized_size<decltype(source)>()> bytes{};
        EXPECT_FALSE(xasrl::serialize(source, std::span{bytes}, xasrl::srl_context{default_context, recorder}));
        EXPECT_EQ(ConvertedValue::conversions, 2);
        std::array<Converted, 2> target{};
        EXPECT_FALSE(xasrl::deserialize(std::span{bytes}, target, default_context));
        EXPECT_EQ(target[0].value.value, 31);
        EXPECT_EQ(target[1].value.value, 42);
        const std::array<Handled, 0> empty{};
        std::array<std::byte, xasrl::serialized_size<decltype(empty)>()> empty_bytes{};
        EXPECT_FALSE(xasrl::serialize(empty, std::span{empty_bytes}, default_context));
    }

    TEST(Status, ContainerFailuresKeepContextAndStopCallingCodecs) {
        using codec = xasrl::leaf_codec<EncodeFailure>;
        const std::vector<EncodeFailure> source{{31}, {42}};
        codec::writes = 0;
        codec::fail = true;
        xasrl::byte_buffer bytes;
        const auto failure = xasrl::serialize(source, bytes, default_context);
        codec::fail = false;
        EXPECT_EQ(failure.code, xasrl::error_code::invalid_reference);
        EXPECT_EQ(failure.id, 0u);
        EXPECT_EQ(failure.offset, 61u);
        EXPECT_EQ(codec::writes, 1);
        ASSERT_FALSE(xasrl::serialize(source, bytes, default_context));
        std::vector<EncodeFailure> target;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), target, default_context));
        EXPECT_EQ(source, target);
    }
}
