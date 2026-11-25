#include "srl/serialization.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <forward_list>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace preparation_tests {
    template<typename T>
    struct counted_sequence {
        std::vector<T> values;
        mutable std::size_t reads{}, steps{};
        struct iterator {
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::forward_iterator_tag;
            const counted_sequence *owner{};
            std::size_t index{};
            const T &operator*() const { ++owner->reads; return owner->values[index]; }
            iterator &operator++() { ++owner->steps; ++index; return *this; }
            iterator operator++(int) { auto old = *this; ++*this; return old; }
            bool operator==(const iterator &) const = default;
        };
        iterator begin() const { return {this, 0}; }
        iterator end() const { return {this, values.size()}; }
        std::size_t size() const { return values.size(); }
        void clear() { values.clear(); }
        void push_back(T value) { values.push_back(std::move(value)); }
    };

    struct fails_encoding { int value{}; };

    struct alignas(64) tracked_storage {
        static inline int live{};
        static inline int moves{};
        int value{};

        tracked_storage() { ++live; }
        tracked_storage(const tracked_storage &) = delete;
        tracked_storage(tracked_storage &&other) noexcept : value(other.value) { ++live; ++moves; }
        ~tracked_storage() { --live; }
    };

    struct storage_key {};
    struct converted {
        [[=xasrl::tag::field{7}, =xasrl::conversion_tag{^^tracked_storage, xasrl::handler_key<storage_key>()}]]
        int value{};
        bool operator==(const converted &) const = default;
    };

    struct tracking_handler : xasrl::typed_reference_handler<int, tracked_storage> {
        int writes{};

        xasrl::error to_storage(const int &source, tracked_storage &target) override {
            ++writes;
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(&target) % alignof(tracked_storage), 0u);
            if (source == -1) { return xasrl::detail::fail(xasrl::error_code::invalid_reference); }
            if (source == -2) { throw std::runtime_error("conversion failed"); }
            target.value = source;
            return {};
        }

        xasrl::error from_storage(int &target, const tracked_storage &source) override {
            target = source.value;
            return {};
        }
    };
}

template<>
struct xasrl::leaf_codec<preparation_tests::fails_encoding> {
    static constexpr xasrl::szt fixed_size = 4;
    static xasrl::error encode(const preparation_tests::fails_encoding &, std::span<std::byte, 4>) {
        return xasrl::detail::fail(xasrl::error_code::invalid_reference);
    }
    static xasrl::always_success decode(std::span<const std::byte, 4>, preparation_tests::fails_encoding &) { return {}; }
};

template<>
struct xasrl::leaf_codec<preparation_tests::tracked_storage> {
    static constexpr xasrl::szt fixed_size = 4;
    static xasrl::always_success encode(const preparation_tests::tracked_storage &source, std::span<std::byte, 4> out) {
        return xasrl::leaf_codec<int>::encode(source.value, out);
    }
    static xasrl::always_success decode(std::span<const std::byte, 4> source, preparation_tests::tracked_storage &out) {
        return xasrl::leaf_codec<int>::decode(source, out.value);
    }
};

namespace preparation_tests {
    template<typename Source>
    concept can_prepare = requires(xasrl::encoding_recorder &recorder, const xasrl::serialization_context &c, Source &&source) {
        xasrl::prepared_serialization<std::remove_cvref_t<Source>>::prepare(recorder, std::forward<Source>(source), c);
    };
    template<typename T>
    concept can_prepare_inline = requires(const xasrl::serialization_context &c, const T &source) {
        xasrl::prepared_serialization<T>::prepare(source, c);
    };
    template<typename T, typename Output>
    concept can_serialize_inline = requires(const xasrl::serialization_context &c, const T &source, Output &out) {
        xasrl::serialize(source, out, c);
    };
    template<typename T>
    concept can_measure_inline = requires(const xasrl::serialization_context &c, const T &source) {
        xasrl::serialized_size(source, c);
    };
    template<typename T>
    concept can_encode_without_recorder = can_prepare_inline<T> && can_measure_inline<T>
        && can_serialize_inline<T, xasrl::byte_buffer> && can_serialize_inline<T, std::span<std::byte>>;

    using nested_sequence = std::vector<std::vector<std::vector<int>>>;
    static_assert(can_prepare<int &> && can_prepare<const int &>);
    static_assert(!can_prepare<int> && !can_prepare<const int>);
    static_assert(can_encode_without_recorder<int>);
    static_assert(can_encode_without_recorder<std::vector<int>>);
    static_assert(can_encode_without_recorder<std::vector<std::array<int, 3>>>);
    static_assert(can_encode_without_recorder<std::array<std::vector<int>, 0>>);
    using converted_sequence = std::vector<std::vector<converted>>;
    static_assert(can_encode_without_recorder<nested_sequence>);
    static_assert(!can_prepare_inline<converted_sequence> && can_prepare<converted_sequence &>);
    static_assert(!can_measure_inline<converted_sequence>);
    static_assert(!can_serialize_inline<converted_sequence, xasrl::byte_buffer>);
    static_assert(!can_serialize_inline<converted_sequence, std::span<std::byte>>);

    struct default_resource_guard {
        std::pmr::memory_resource *old = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        ~default_resource_guard() { std::pmr::set_default_resource(old); }
    };

    class counting_resource final : public std::pmr::memory_resource {
    public:
        std::size_t allocations{}, bytes{};
        std::size_t allocation_limit = std::numeric_limits<std::size_t>::max();

    private:
        void *do_allocate(std::size_t size, std::size_t alignment) override {
            if (allocations == allocation_limit) { throw std::bad_alloc{}; }
            auto *ptr = std::pmr::new_delete_resource()->allocate(size, alignment);
            ++allocations;
            bytes += size;
            return ptr;
        }

        void do_deallocate(void *ptr, std::size_t size, std::size_t alignment) override {
            std::pmr::new_delete_resource()->deallocate(ptr, size, alignment);
            bytes -= size;
        }

        bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
    };

    TEST(Preparation, FixedAndDynamicSpansHaveCorrectStatus) {
        xasrl::serialization_context context;
        const int source = 42;
        auto prepared = xasrl::prepared_serialization<int>::prepare(source, context);
        ASSERT_TRUE(prepared);
        std::array<std::byte, xasrl::serialized_size<int>()> bytes;
        static_assert(std::same_as<decltype(prepared->encode(std::span{bytes})), xasrl::always_success>);
        static_assert(std::same_as<decltype(prepared->encode(std::span<std::byte>{bytes})), xasrl::error>);
        EXPECT_FALSE(prepared->encode(std::span{bytes}));
        EXPECT_FALSE(prepared->encode(std::span<std::byte>{bytes}));
        EXPECT_EQ(prepared->encode(std::span<std::byte>{bytes}.first(1)).code, xasrl::error_code::invalid_entry_size);
        int target{};
        ASSERT_FALSE(xasrl::deserialize(std::span{bytes}, target, context));
        EXPECT_EQ(target, source);
    }

    TEST(Preparation, NestedUnsizedContainersNeedNoRecords) {
        using sequence = std::vector<std::forward_list<int>>;
        static_assert(!std::ranges::sized_range<std::forward_list<int>>);
        static_assert(can_encode_without_recorder<sequence>);
        const sequence source{{1, 2}, {}, {3, 4, 5}};
        const std::vector<std::vector<int>> sized{{1, 2}, {}, {3, 4, 5}};
        const xasrl::serialization_context context;
        xasrl::encoding_recorder recorder{*std::pmr::null_memory_resource()};
        const auto prepared = xasrl::prepared_serialization<sequence>::prepare(source, context);
        ASSERT_TRUE(prepared);
        EXPECT_EQ(prepared->size(), 212u);
        xasrl::byte_buffer bytes, expected;
        ASSERT_FALSE(xasrl::serialize(source, bytes, xasrl::srl_context{context, recorder}));
        ASSERT_FALSE(xasrl::serialize(sized, expected, context));
        EXPECT_EQ(bytes, expected);
        EXPECT_EQ(recorder.retained_bytes(), 0u);
        sequence target;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), target, context));
        EXPECT_EQ(target, source);
    }

    TEST(Preparation, NestedRecordsUseOnlyTheSuppliedResourceAndReuseBlocks) {
        const converted_sequence source(128, {converted{1}, converted{2}});
        tracking_handler handler;
        xasrl::serialization_context context;
        context.register_handler(xasrl::handler_key<storage_key>(), handler);
        counting_resource resource;
        xasrl::encoding_recorder recorder{resource};
        default_resource_guard no_default_allocation;
        xasrl::byte_buffer bytes;
        xasrl::byte_buffer repeated;
        using prepared_type = xasrl::prepared_serialization<converted_sequence>;
        {
            auto prepared = prepared_type::prepare(recorder, source, context);
            ASSERT_TRUE(prepared);
            auto moved = std::move(*prepared);
            bytes.resize(moved.size());
            repeated.resize(moved.size());
            ASSERT_FALSE(moved.encode(bytes.span()));
            ASSERT_FALSE(moved.encode(repeated.span()));
            EXPECT_EQ(repeated, bytes);
            EXPECT_THROW((void)prepared_type::prepare(recorder, source, context), std::logic_error);
            EXPECT_THROW(recorder.release(), std::logic_error);
        }
        const auto allocations = resource.allocations;
        EXPECT_GT(allocations, 1u);
        EXPECT_EQ(recorder.retained_bytes(), resource.bytes);
        ASSERT_FALSE(xasrl::serialize(source, repeated.span(), xasrl::srl_context{context, recorder}));
        EXPECT_EQ(repeated, bytes);
        EXPECT_EQ(resource.allocations, allocations);
        converted_sequence target;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), target, context));
        EXPECT_EQ(target, source);
        recorder.release();
        EXPECT_EQ(recorder.retained_bytes(), 0u);
        EXPECT_EQ(resource.bytes, 0u);
    }

    TEST(Preparation, AllocationFailureReleasesTheRecorderForReuse) {
        counting_resource resource;
        resource.allocation_limit = 1;
        xasrl::encoding_recorder recorder{resource};
        tracking_handler handler;
        xasrl::serialization_context context;
        context.register_handler(xasrl::handler_key<storage_key>(), handler);
        const converted_sequence large(128, {converted{1}, converted{2}});
        const converted_sequence small{{converted{1}}};
        EXPECT_THROW((void)xasrl::serialized_size(recorder, large, context), std::bad_alloc);
        ASSERT_EQ(resource.allocations, 1u);
        ASSERT_TRUE(xasrl::serialized_size(recorder, small, context));
        EXPECT_EQ(resource.allocations, 1u);
    }

    TEST(Preparation, ConversionObjectsSurviveEncodingAndAreDestroyedOnEveryExit) {
        using sequence = std::vector<converted>;
        using prepared_type = xasrl::prepared_serialization<sequence>;
        xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
        tracking_handler handler;
        xasrl::serialization_context context;
        context.register_handler(xasrl::handler_key<storage_key>(), handler);
        const sequence rejected{{17}, {-1}};
        const auto failure = prepared_type::prepare(recorder, rejected, context);
        ASSERT_FALSE(failure);
        EXPECT_EQ(failure.error().code, xasrl::error_code::invalid_reference);
        EXPECT_EQ(failure.error().id, 7u);
        EXPECT_EQ(tracked_storage::live, 0);
        const sequence throwing{{17}, {-2}};
        EXPECT_THROW((void)prepared_type::prepare(recorder, throwing, context), std::runtime_error);
        EXPECT_EQ(tracked_storage::live, 0);

        const sequence source{{17}, {42}};
        handler.writes = 0;
        tracked_storage::moves = 0;
        {
            auto prepared = prepared_type::prepare(recorder, source, context);
            ASSERT_TRUE(prepared);
            auto moved = std::move(*prepared);
            EXPECT_EQ(tracked_storage::live, 2);
            xasrl::byte_buffer bytes;
            bytes.resize(moved.size());
            ASSERT_FALSE(moved.encode(bytes.span()));
            ASSERT_FALSE(moved.encode(bytes.span()));
            EXPECT_EQ(handler.writes, 2);
            EXPECT_EQ(tracked_storage::moves, 0);
            sequence target;
            ASSERT_FALSE(xasrl::deserialize(bytes.span(), target, context));
            ASSERT_EQ(target.size(), 2u);
            EXPECT_EQ(target[0].value, 17);
            EXPECT_EQ(target[1].value, 42);
        }
        EXPECT_EQ(tracked_storage::live, 0);
    }

    TEST(Preparation, FixedElementsMeasureWithoutVisitingThem) {
        counted_sequence<int> source{{1, 2, 3, 4}};
        xasrl::serialization_context context;
        const auto size = xasrl::serialized_size(source, context);
        ASSERT_TRUE(size);
        EXPECT_EQ(*size, 60 + source.size() * 4);
        EXPECT_EQ(source.reads, 0);
        EXPECT_EQ(source.steps, 0);
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(source, bytes, context));
        EXPECT_EQ(source.reads, source.size());
        EXPECT_EQ(source.steps, source.size());
    }

    TEST(Preparation, EncoderStopsTheOuterTraversalAtFirstError) {
        counted_sequence<fails_encoding> source{{{1}, {2}, {3}}};
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        const auto status = xasrl::serialize(source, bytes, context);
        EXPECT_EQ(status.code, xasrl::error_code::invalid_reference);
        EXPECT_EQ(status.id, 0u);
        EXPECT_EQ(source.reads, 1);
        EXPECT_EQ(source.steps, 0);
    }
}
