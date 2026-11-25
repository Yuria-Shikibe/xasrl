#include "test_support.hpp"

#include <memory_resource>

namespace {
    class forbidden_resource final : public std::pmr::memory_resource {
        void *do_allocate(std::size_t, std::size_t) override { throw std::bad_alloc{}; }
        void do_deallocate(void *, std::size_t, std::size_t) override {}
        bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
    };

    TEST(ResourceLimits, InputLimitPrecedesAllocationAndHandlesExactBounds) {
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(std::vector<int>{1, 2, 3}, bytes, context));
        forbidden_resource memory;
        std::pmr::vector<int> out{&memory};
        context.config.limits.max_input_bytes = bytes.size() - 1;
        EXPECT_EQ(xasrl::deserialize(bytes.span(), out, context).code, xasrl::error_code::resource_limit_exceeded);
        EXPECT_TRUE(out.empty());

        std::vector<int> ordinary;
        context.config.limits.max_input_bytes = bytes.size();
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), ordinary, context));
        EXPECT_EQ(ordinary, (std::vector<int>{1, 2, 3}));
    }

    TEST(ResourceLimits, NestedContainersRoundTripWithinInputLimit) {
        const std::vector<std::vector<int>> source{{1, 2, 3}, {4, 5, 6}};
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
        ASSERT_FALSE(xasrl::serialize(source, bytes, xasrl::srl_context{context, recorder}));
        context.config.limits.max_input_bytes = bytes.size() - 1;
        std::vector<std::vector<int>> out{{99}};
        EXPECT_EQ(xasrl::deserialize(bytes.span(), out, context).code, xasrl::error_code::resource_limit_exceeded);
        EXPECT_EQ(out, (std::vector<std::vector<int>>{{99}}));
        context.config.limits.max_input_bytes = bytes.size();
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out, source);
        ASSERT_FALSE(xasrl::deserialize(bytes.span(), out, context));
    }

    TEST(ResourceLimits, FileBudgetPrecedesAllocation) {
        const auto path = std::filesystem::temp_directory_path() / "srl_read_budget.bin";
        xasrl::serialization_context context;
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(42, bytes, context));
        ASSERT_FALSE(xasrl::write_block(path, bytes.span()));
        auto rejected = xasrl::read_block(path, bytes.size() - 1);
        ASSERT_FALSE(rejected);
        EXPECT_EQ(rejected.error().code, xasrl::error_code::resource_limit_exceeded);
        auto accepted = xasrl::read_block(path, bytes.size());
        ASSERT_TRUE(accepted);
        EXPECT_EQ(*accepted, bytes);
        EXPECT_TRUE(std::filesystem::remove(path));
    }
}
