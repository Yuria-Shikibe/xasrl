#include "test_support.hpp"

#include <memory_resource>

namespace {
    class forbidden_resource final : public std::pmr::memory_resource {
        void *do_allocate(std::size_t, std::size_t) override { throw std::bad_alloc{}; }
        void do_deallocate(void *, std::size_t, std::size_t) override {}
        bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
    };

    TEST(ResourceLimits, InputLimitPrecedesAllocationAndHandlesExactBounds) {
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(std::vector<int>{1, 2, 3}, bytes, context));
        forbidden_resource memory;
        std::pmr::vector<int> out{&memory};
        context.config.limits.max_input_bytes = bytes.size() - 1;
        EXPECT_EQ(srl::deserialize(bytes.span(), out, context).code, srl::error_code::resource_limit_exceeded);
        EXPECT_TRUE(out.empty());

        std::vector<int> ordinary;
        context.config.limits.max_input_bytes = bytes.size();
        ASSERT_FALSE(srl::deserialize(bytes.span(), ordinary, context));
        EXPECT_EQ(ordinary, (std::vector<int>{1, 2, 3}));
    }

    TEST(ResourceLimits, NestedContainersRoundTripWithinInputLimit) {
        const std::vector<std::vector<int>> source{{1, 2, 3}, {4, 5, 6}};
        srl::serialization_context context;
        srl::byte_buffer bytes;
        srl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
        ASSERT_FALSE(srl::serialize(source, bytes, srl::srl_context{context, recorder}));
        context.config.limits.max_input_bytes = bytes.size() - 1;
        std::vector<std::vector<int>> out{{99}};
        EXPECT_EQ(srl::deserialize(bytes.span(), out, context).code, srl::error_code::resource_limit_exceeded);
        EXPECT_EQ(out, (std::vector<std::vector<int>>{{99}}));
        context.config.limits.max_input_bytes = bytes.size();
        ASSERT_FALSE(srl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(out, source);
        ASSERT_FALSE(srl::deserialize(bytes.span(), out, context));
    }

    TEST(ResourceLimits, FileBudgetPrecedesAllocation) {
        const auto path = std::filesystem::temp_directory_path() / "srl_read_budget.bin";
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(42, bytes, context));
        ASSERT_FALSE(srl::write_block(path, bytes.span()));
        auto rejected = srl::read_block(path, bytes.size() - 1);
        ASSERT_FALSE(rejected);
        EXPECT_EQ(rejected.error().code, srl::error_code::resource_limit_exceeded);
        auto accepted = srl::read_block(path, bytes.size());
        ASSERT_TRUE(accepted);
        EXPECT_EQ(*accepted, bytes);
        EXPECT_TRUE(std::filesystem::remove(path));
    }
}
