#include "srl/storage/byte_buffer.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
    struct allocation_state {
        std::map<std::byte *, std::size_t> allocations;
        bool fail{};
        ~allocation_state() { EXPECT_TRUE(allocations.empty()); }
    };

    template<bool Propagate>
    struct byte_allocator {
        using value_type = std::byte;
        using propagate_on_container_copy_assignment = std::bool_constant<Propagate>;
        using propagate_on_container_move_assignment = std::bool_constant<Propagate>;
        using propagate_on_container_swap = std::bool_constant<Propagate>;
        using is_always_equal = std::false_type;
        allocation_state *state;

        std::byte *allocate(std::size_t count) {
            if (state->fail) { throw std::bad_alloc{}; }
            auto *result = std::allocator<std::byte>{}.allocate(count);
            std::fill_n(result, count, std::byte{0xa5});
            state->allocations.emplace(result, count);
            return result;
        }
        void deallocate(std::byte *pointer, std::size_t count) noexcept {
            const auto found = state->allocations.find(pointer);
            ASSERT_NE(found, state->allocations.end());
            EXPECT_EQ(found->second, count);
            state->allocations.erase(found);
            std::allocator<std::byte>{}.deallocate(pointer, count);
        }
        bool operator==(const byte_allocator &) const = default;
    };

    TEST(ByteBuffer, ResizeReuseAndValueSemantics) {
        xasrl::byte_buffer bytes;
        EXPECT_TRUE(bytes.empty());
        EXPECT_EQ(bytes.begin(), bytes.end());
        bytes.reserve(32);
        auto *pointer = bytes.data();
        bytes.resize(4, std::byte{7});
        auto copy = bytes;
        copy[0] = std::byte{8};
        EXPECT_NE(copy, bytes);
        bytes.resize(2);
        bytes.resize(4);
        EXPECT_EQ(bytes[0], std::byte{7});
        EXPECT_EQ(bytes[2], std::byte{});
        EXPECT_EQ(bytes.data(), pointer);
        bytes.push_back(std::byte{9});
        auto moved = std::move(bytes);
        EXPECT_EQ(moved.data(), pointer);
        EXPECT_TRUE(bytes.empty());
        EXPECT_EQ(bytes.capacity(), 0u);
        bytes = moved;
        EXPECT_EQ(bytes, moved);
        bytes.clear();
        EXPECT_GE(bytes.capacity(), moved.size());
        bytes = std::move(moved);
        EXPECT_EQ(bytes.data(), pointer);
        EXPECT_TRUE(moved.empty());
    }

    TEST(ByteBuffer, UninitializedGrowthAndOverwrite) {
        allocation_state state;
        xasrl::storage::basic_byte_buffer<byte_allocator<false>> bytes{byte_allocator<false>{&state}};
        bytes.resize_uninitialized(8);
        // The allocator explicitly initialized these bytes: growth must not overwrite them.
        EXPECT_TRUE(std::ranges::all_of(bytes, [](auto value) { return value == std::byte{0xa5}; }));
        auto *pointer = bytes.data();
        bytes.resize_and_overwrite(8, [](std::span<std::byte> output) -> std::size_t {
            std::fill_n(output.begin(), 3, std::byte{42});
            return 3;
        });
        EXPECT_EQ(bytes.size(), 3u);
        EXPECT_EQ(bytes.data(), pointer);
        EXPECT_EQ(bytes[2], std::byte{42});
        bytes.resize_and_overwrite(8, [](std::span<std::byte> output) noexcept -> std::size_t {
            std::fill_n(output.begin(), 5, std::byte{23});
            return 5;
        });
        EXPECT_EQ(bytes.size(), 5u);
        EXPECT_EQ(bytes.data(), pointer);
        EXPECT_EQ(bytes[4], std::byte{23});
        EXPECT_THROW(bytes.resize_and_overwrite(8, [](std::span<std::byte>) -> std::size_t {
            throw std::runtime_error("writer failed");
        }), std::runtime_error);
        EXPECT_TRUE(bytes.empty());
        EXPECT_EQ(bytes.data(), pointer);
        bytes.resize_and_overwrite(0, [](std::span<std::byte> output) -> std::size_t {
            EXPECT_TRUE(output.empty());
            return 0;
        });
        bytes.resize(2, std::byte{1});
        state.fail = true;
        bool called = false;
        EXPECT_THROW(bytes.resize_and_overwrite(100, [&](std::span<std::byte>) -> std::size_t {
            called = true;
            return 0;
        }), std::bad_alloc);
        EXPECT_FALSE(called);
        EXPECT_EQ(bytes.size(), 2u);
        EXPECT_EQ(bytes[0], std::byte{1});
        EXPECT_EQ(bytes.data(), pointer);
        EXPECT_THROW(bytes.reserve(bytes.max_size() + 1), std::length_error);
    }

    template<bool Propagate>
    void check_allocators() {
        allocation_state first, second;
        using allocator = byte_allocator<Propagate>;
        using buffer = xasrl::storage::basic_byte_buffer<allocator>;
        buffer source{allocator{&first}};
        source.resize(3, std::byte{5});
        buffer target{allocator{&second}};
        target.resize(2, std::byte{9});
        target = source;
        EXPECT_EQ(target, source);
        EXPECT_EQ(target.get_allocator().state, Propagate ? &first : &second);
        buffer copied(source, allocator{&second});
        EXPECT_EQ(copied, source);
        EXPECT_EQ(copied.get_allocator().state, &second);
        auto *source_pointer = source.data();
        copied = std::move(source);
        EXPECT_EQ(copied, target);
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(copied.get_allocator().state, Propagate ? &first : &second);
        if constexpr (Propagate) { EXPECT_EQ(copied.data(), source_pointer); }
        else { EXPECT_NE(copied.data(), source_pointer); }
        buffer same_allocator{copied.get_allocator()};
        auto *moved_pointer = copied.data();
        same_allocator = std::move(copied);
        EXPECT_EQ(same_allocator.data(), moved_pointer);
        buffer swapped{allocator{Propagate ? &second : same_allocator.get_allocator().state}};
        swap(swapped, same_allocator);
        EXPECT_EQ(swapped.data(), moved_pointer);
        EXPECT_TRUE(same_allocator.empty());
        first.fail = second.fail = true;
        const auto before = swapped.data();
        EXPECT_THROW(swapped = target, std::bad_alloc);
        EXPECT_EQ(swapped.data(), before);
        EXPECT_EQ(swapped, target);
    }

    TEST(ByteBuffer, NonPropagatingAllocators) { check_allocators<false>(); }
    TEST(ByteBuffer, PropagatingAllocators) { check_allocators<true>(); }
}
