#include "test_support.hpp"

#include <array>
#include <memory_resource>
#include <vector>

namespace {
    struct [[=srl::tag::auto_seq{}]] allocated_record {
        std::pmr::vector<int> values;
        std::array<std::pmr::vector<int>, 2> nested;
        [[=srl::tag::transient{}]] std::pmr::vector<int> cache;
    };

    struct [[=srl::tag::auto_seq{}]] allocated_native_arrays {
        std::pmr::vector<int> values[2][2];
        int tail;
    };

    // Detect resource mismatch without allowing an invalid free to reach the heap.
    class tracking_resource final : public std::pmr::memory_resource {
        std::pmr::memory_resource *upstream = std::pmr::new_delete_resource();
        std::map<void *, std::pair<std::size_t, std::size_t>> allocations;

        void *do_allocate(std::size_t bytes, std::size_t alignment) override {
            auto *pointer = upstream->allocate(bytes, alignment);
            allocations.emplace(pointer, std::pair{bytes, alignment});
            return pointer;
        }

        void do_deallocate(void *pointer, std::size_t bytes, std::size_t alignment) override {
            const auto found = allocations.find(pointer);
            ASSERT_NE(found, allocations.end());
            EXPECT_EQ(found->second.first, bytes);
            EXPECT_EQ(found->second.second, alignment);
            allocations.erase(found);
            upstream->deallocate(pointer, bytes, alignment);
        }

        bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override {
            return this == &other;
        }

    public:
        ~tracking_resource() override { EXPECT_TRUE(allocations.empty()); }
    };

    TEST(Transaction, PreservesAllocatorForRootRecordAndArrays) {
        tracking_resource memory;
        srl::serialization_context context;
        srl::byte_buffer bytes;
        const std::vector<int> source{1, 2, 3};
        ASSERT_FALSE(srl::serialize(source, bytes, context));
        std::pmr::vector<int> root({8, 9}, &memory);
        ASSERT_FALSE(srl::deserialize(bytes.span(), root, context));
        EXPECT_EQ(root.get_allocator().resource(), &memory);
        EXPECT_TRUE(std::ranges::equal(root, source));

        allocated_record record{
            std::pmr::vector<int>({4}, &memory),
            {std::pmr::vector<int>({5}, &memory), std::pmr::vector<int>({6}, &memory)},
            std::pmr::vector<int>({99}, &memory),
        };
        auto copy = srl::transaction_traits<allocated_record>::clone(record);
        EXPECT_EQ(copy.values.get_allocator().resource(), &memory);
        EXPECT_EQ(copy.nested[1].get_allocator().resource(), &memory);
        EXPECT_EQ(copy.cache.get_allocator().resource(), &memory);
        ASSERT_FALSE(srl::serialize(record, bytes, context));
        record.values[0] = 42;
        ASSERT_FALSE(srl::deserialize(bytes.span(), record, context));
        EXPECT_EQ(record.values[0], 4);
        EXPECT_EQ(record.cache[0], 99);
        EXPECT_EQ(record.nested[0].get_allocator().resource(), &memory);

        std::pmr::vector<int> array[]{std::pmr::vector<int>({1}, &memory)};
        ASSERT_FALSE(srl::serialize(array, bytes, context));
        ASSERT_FALSE(srl::deserialize(bytes.span(), array, context));
        EXPECT_EQ(array[0].get_allocator().resource(), &memory);
    }

    TEST(Transaction, MalformedInputPreservesAllocatorAndContents) {
        tracking_resource memory;
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(std::vector<int>{1, 2}, bytes, context));
        bytes[11] = std::byte{0};
        std::pmr::vector<int> out({99}, &memory);
        EXPECT_TRUE(srl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out.get_allocator().resource(), &memory);
        ASSERT_EQ(out.size(), 1);
        EXPECT_EQ(out[0], 99);
    }

    TEST(Transaction, AggregateNativeArrayClonePreservesEveryAllocator) {
        tracking_resource memory;
        allocated_native_arrays value{{{
            std::pmr::vector<int>({1}, &memory), std::pmr::vector<int>({2}, &memory)}, {
            std::pmr::vector<int>({3}, &memory), std::pmr::vector<int>({4}, &memory)}}, 7};
        auto staged = srl::transaction_traits<allocated_native_arrays>::clone(value);
        for (const auto &row: staged.values) {
            for (const auto &element: row) {
                EXPECT_EQ(element.get_allocator().resource(), &memory);
            }
        }
        EXPECT_EQ(staged.tail, 7);
        staged.values[1][1][0] = 42;
        srl::transaction_traits<allocated_native_arrays>::commit(value, staged);
        EXPECT_EQ(value.values[1][1][0], 42);
        EXPECT_EQ(staged.values[1][1][0], 4);
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(value, bytes, context));
        value.values[0][0][0] = 99;
        ASSERT_FALSE(srl::deserialize(bytes.span(), value, context));
        EXPECT_EQ(value.values[0][0][0], 1);
        EXPECT_EQ(value.values[1][1].get_allocator().resource(), &memory);
    }

    struct allocated_base {
        std::pmr::vector<int> prefix;
    };

    struct mixed_allocated_arrays : allocated_base {
        std::pmr::vector<int> first[2];
        int marker;
        std::pmr::vector<int> second[1][2];
    };

    TEST(Transaction, CloneMapsBasesAndSeparateArraysToTheirOwnResources) {
        tracking_resource first_resource;
        tracking_resource second_resource;
        mixed_allocated_arrays original{
            {std::pmr::vector<int>({10}, &first_resource)},
            {std::pmr::vector<int>({20}, &first_resource), std::pmr::vector<int>({30}, &second_resource)},
            40,
            {{std::pmr::vector<int>({50}, &second_resource), std::pmr::vector<int>({60}, &first_resource)}},
        };
        auto copy = srl::transaction_traits<mixed_allocated_arrays>::clone(original);
        EXPECT_EQ(copy.prefix.get_allocator().resource(), &first_resource);
        EXPECT_EQ(copy.prefix[0], 10);
        EXPECT_EQ(copy.first[0].get_allocator().resource(), &first_resource);
        EXPECT_EQ(copy.first[1].get_allocator().resource(), &second_resource);
        EXPECT_EQ(copy.first[0][0], 20);
        EXPECT_EQ(copy.first[1][0], 30);
        EXPECT_EQ(copy.marker, 40);
        EXPECT_EQ(copy.second[0][0].get_allocator().resource(), &second_resource);
        EXPECT_EQ(copy.second[0][1].get_allocator().resource(), &first_resource);
        EXPECT_EQ(copy.second[0][0][0], 50);
        EXPECT_EQ(copy.second[0][1][0], 60);
        copy.first[1][0] = 99;
        srl::transaction_traits<mixed_allocated_arrays>::commit(original, copy);
        EXPECT_EQ(original.first[1][0], 99);
        EXPECT_EQ(copy.first[1][0], 30);
    }

}
