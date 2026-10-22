#pragma once

#include <meta>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace srl::storage {
    template<typename Allocator = std::allocator<std::byte>>
        requires std::same_as<typename std::allocator_traits<Allocator>::value_type, std::byte>
    class basic_byte_buffer {
        using traits = std::allocator_traits<Allocator>;
        using pointer = typename traits::pointer;

    public:
        using value_type = std::byte;
        using allocator_type = Allocator;
        using size_type = std::size_t;
        using iterator = value_type *;
        using const_iterator = const value_type *;

        basic_byte_buffer() = default;
        explicit basic_byte_buffer(const allocator_type &allocator) : allocator_(allocator) {}

        explicit basic_byte_buffer(std::span<const std::byte> bytes,
                                   const allocator_type &allocator = allocator_type{})
            : allocator_(allocator) {
            reserve(bytes.size());
            if (!bytes.empty()) { std::memcpy(data(), bytes.data(), bytes.size()); }
            size_ = bytes.size();
        }

        basic_byte_buffer(const basic_byte_buffer &other)
            : basic_byte_buffer(other.span(), traits::select_on_container_copy_construction(other.allocator_)) {}

        basic_byte_buffer(const basic_byte_buffer &other, const allocator_type &allocator)
            : basic_byte_buffer(other.span(), allocator) {}

        basic_byte_buffer(basic_byte_buffer &&other) noexcept(std::meta::is_nothrow_move_constructible_type(^^Allocator))
            : allocator_(std::move(other.allocator_)) { take_storage(other); }

        ~basic_byte_buffer() { release(); }

        basic_byte_buffer &operator=(const basic_byte_buffer &other) {
            if (this == &other) { return *this; }
            if constexpr (traits::propagate_on_container_copy_assignment::value) {
                basic_byte_buffer replacement(other, other.allocator_);
                release();
                allocator_ = other.allocator_;
                take_storage(replacement);
            } else {
                basic_byte_buffer replacement(other, allocator_);
                swap_storage(replacement);
            }
            return *this;
        }

        basic_byte_buffer &operator=(basic_byte_buffer &&other)
            noexcept(traits::propagate_on_container_move_assignment::value
                ? std::meta::is_nothrow_move_assignable_type(^^Allocator) : traits::is_always_equal::value) {
            if (this == &other) { return *this; }
            if constexpr (traits::propagate_on_container_move_assignment::value) {
                release();
                allocator_ = std::move(other.allocator_);
                take_storage(other);
            } else if constexpr (traits::is_always_equal::value) {
                release();
                take_storage(other);
            } else if (allocator_ == other.allocator_) {
                release();
                take_storage(other);
            } else {
                basic_byte_buffer replacement(other, allocator_);
                swap_storage(replacement);
                other.clear();
            }
            return *this;
        }

        // As for allocator-aware standard containers, non-propagating allocators must compare equal.
        friend void swap(basic_byte_buffer &left, basic_byte_buffer &right)
            noexcept(!traits::propagate_on_container_swap::value || std::meta::is_nothrow_swappable_type(^^Allocator)) {
            if constexpr (traits::propagate_on_container_swap::value) {
                using std::swap;
                swap(left.allocator_, right.allocator_);
            }
            left.swap_storage(right);
        }

        [[nodiscard]] allocator_type get_allocator() const { return allocator_; }
        [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
        [[nodiscard]] size_type size() const noexcept { return size_; }
        [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
        [[nodiscard]] size_type max_size() const noexcept {
            return std::min<size_type>(traits::max_size(allocator_), std::numeric_limits<std::ptrdiff_t>::max());
        }
        [[nodiscard]] value_type *data() noexcept { return std::to_address(storage_); }
        [[nodiscard]] const value_type *data() const noexcept { return std::to_address(storage_); }
        [[nodiscard]] std::span<std::byte> span() noexcept { return {data(), size_}; }
        [[nodiscard]] std::span<const std::byte> span() const noexcept { return {data(), size_}; }
        [[nodiscard]] iterator begin() noexcept { return data(); }
        [[nodiscard]] const_iterator begin() const noexcept { return data(); }
        [[nodiscard]] iterator end() noexcept { return empty() ? data() : data() + size_; }
        [[nodiscard]] const_iterator end() const noexcept { return empty() ? data() : data() + size_; }
        [[nodiscard]] value_type &operator[](size_type index) noexcept { return data()[index]; }
        [[nodiscard]] const value_type &operator[](size_type index) const noexcept { return data()[index]; }

        void reserve(size_type requested_capacity) {
            if (requested_capacity <= capacity_) { return; }
            const auto maximum = max_size();
            if (requested_capacity > maximum) { throw std::length_error("byte buffer capacity exceeds max_size"); }
            const auto grown = capacity_ > maximum - capacity_ ? maximum : capacity_ * 2;
            const auto capacity = std::max(requested_capacity, grown);
            auto storage = traits::allocate(allocator_, capacity);
            if (size_ != 0) { std::memcpy(std::to_address(storage), data(), size_); }
            if (capacity_ != 0) { traits::deallocate(allocator_, storage_, capacity_); }
            storage_ = storage;
            capacity_ = capacity;
        }

        void resize(size_type size, std::byte value = {}) {
            reserve(size);
            if (size > size_) { std::fill_n(data() + size_, size - size_, value); }
            size_ = size;
        }

        // Newly exposed bytes must be written before they are read.
        void resize_uninitialized(size_type size) {
            reserve(size);
            size_ = size;
        }

        // writer initializes the returned prefix, returns a length <= size, and must not
        // mutate this buffer through another alias. Exceptions leave an empty reusable buffer.
        template<typename Writer>
            requires std::invocable<Writer, std::span<std::byte>>
                && std::same_as<std::invoke_result_t<Writer, std::span<std::byte>>, size_type>
        void resize_and_overwrite(size_type size, Writer &&writer) {
            reserve(size);
            size_ = 0;
            size_ = std::invoke(std::forward<Writer>(writer), std::span<std::byte>{data(), size});
        }

        void clear() noexcept { size_ = 0; }
        void push_back(std::byte value) {
            if (size_ == max_size()) { throw std::length_error("byte buffer is full"); }
            reserve(size_ + 1);
            data()[size_++] = value;
        }

        friend bool operator==(const basic_byte_buffer &left, const basic_byte_buffer &right) noexcept {
            return left.size_ == right.size_
                && (left.empty() || std::memcmp(left.data(), right.data(), left.size_) == 0);
        }

    private:
        void release() noexcept {
            if (capacity_ != 0) { traits::deallocate(allocator_, storage_, capacity_); }
            storage_ = pointer{};
            size_ = capacity_ = 0;
        }

        void take_storage(basic_byte_buffer &other) noexcept {
            storage_ = std::exchange(other.storage_, pointer{});
            size_ = std::exchange(other.size_, 0);
            capacity_ = std::exchange(other.capacity_, 0);
        }

        void swap_storage(basic_byte_buffer &other) noexcept {
            using std::swap;
            swap(storage_, other.storage_);
            swap(size_, other.size_);
            swap(capacity_, other.capacity_);
        }

        [[no_unique_address]] allocator_type allocator_{};
        pointer storage_{};
        size_type size_{};
        size_type capacity_{};
    };

    using byte_buffer = basic_byte_buffer<>;
}

namespace srl {
    using byte_buffer = storage::byte_buffer;
}
