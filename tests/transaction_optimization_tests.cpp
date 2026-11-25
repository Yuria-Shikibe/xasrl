#include "srl/serialization.hpp"

#include <gtest/gtest.h>
#include <inplace_vector>
#include <memory_resource>
#include <vector>

namespace {
    class counting_resource final : public std::pmr::memory_resource {
        void *do_allocate(std::size_t bytes, std::size_t alignment) override {
            auto *result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
            ++allocations;
            return result;
        }
        void do_deallocate(void *pointer, std::size_t bytes, std::size_t alignment) override {
            std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
    public:
        std::size_t allocations{};
    };

    static_assert(xasrl::transactional<std::inplace_vector<int, 2>>);
    // Unlike vector's storage swap, inplace_vector swaps individual elements.
    // Reconstructed PMR elements need not share the old elements' resources.
    static_assert(!xasrl::transactional<std::inplace_vector<std::pmr::vector<int>, 2>>);

    struct allocator_owner {
        std::pmr::vector<int> values;
        explicit allocator_owner(std::pmr::memory_resource *resource) : values(resource) {}
    };
    // Copy construction selects a new PMR resource, but this non-aggregate has
    // no allocator-preserving constructor or transaction customization.
    static_assert(!xasrl::transactional<allocator_owner>);

    TEST(TransactionOptimization, VectorReplacementDoesNotCloneDiscardedElements) {
        const xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(std::vector<int>{}, bytes, context));
        counting_resource memory;
        std::pmr::vector<int> out(&memory);
        out.resize(1024, 9);
        memory.allocations = 0;
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
        EXPECT_TRUE(out.empty());
        EXPECT_EQ(memory.allocations, 0);
        EXPECT_EQ(out.get_allocator().resource(), &memory);
    }

    TEST(TransactionOptimization, VectorCloneStillPreservesValue) {
        counting_resource memory;
        const std::pmr::vector<int> source({3, 4}, &memory);
        const auto copy = xasrl::transaction_traits<std::pmr::vector<int>>::clone(source);
        EXPECT_EQ(copy, source);
        EXPECT_EQ(copy.get_allocator().resource(), &memory);
    }

    TEST(TransactionOptimization, FailedVectorReplacementKeepsValueAndAllocator) {
        const xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(std::vector<bool>{true}, bytes, context));
        bytes[bytes.size() - 1] = std::byte{2};
        counting_resource memory;
        std::pmr::vector<bool> out({false, true}, &memory);
        EXPECT_EQ(xasrl::deserialize(bytes.span(), out, context).code, xasrl::error_code::invalid_bool);
        ASSERT_EQ(out.size(), 2);
        EXPECT_FALSE(out[0]);
        EXPECT_TRUE(out[1]);
        EXPECT_EQ(out.get_allocator().resource(), &memory);
    }

    TEST(TransactionOptimization, InplaceVectorRoundTripAndOverflow) {
        const xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(std::vector<int>{2, 3}, bytes, context));
        std::inplace_vector<int, 2> out{9};
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
        ASSERT_EQ(out.size(), 2);
        EXPECT_EQ(out[0], 2);
        EXPECT_EQ(out[1], 3);
        ASSERT_FALSE(xasrl::serialize(std::vector<int>{4, 5, 6}, bytes, context));
        EXPECT_EQ(xasrl::deserialize(bytes.span(), out, context).code, xasrl::error_code::resource_limit_exceeded);
        ASSERT_EQ(out.size(), 2);
        EXPECT_EQ(out[0], 2);
        EXPECT_EQ(out[1], 3);
    }
}
