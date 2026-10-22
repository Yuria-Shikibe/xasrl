#pragma once

#include <concepts>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace srl {
    namespace storage {
        template<typename Allocator = std::allocator<std::byte>>
            requires std::same_as<typename std::allocator_traits<Allocator>::value_type, std::byte>
        class basic_byte_buffer {
        public:
            using value_type = std::byte;
            using allocator_type = Allocator;
            using size_type = std::size_t;
            using iterator = value_type *;
            using const_iterator = const value_type *;

            basic_byte_buffer() = default;

            explicit basic_byte_buffer(const allocator_type &allocator)
                : bytes_(allocator) {}

            explicit basic_byte_buffer(std::span<const std::byte> bytes,
                                       const allocator_type &allocator = allocator_type{})
                : bytes_(bytes.begin(), bytes.end(), allocator) {}

            [[nodiscard]] allocator_type get_allocator() const { return bytes_.get_allocator(); }
            [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }
            [[nodiscard]] size_type size() const noexcept { return bytes_.size(); }
            [[nodiscard]] size_type capacity() const noexcept { return bytes_.capacity(); }
            [[nodiscard]] size_type max_size() const noexcept { return bytes_.max_size(); }
            [[nodiscard]] value_type *data() noexcept { return bytes_.data(); }
            [[nodiscard]] const value_type *data() const noexcept { return bytes_.data(); }
            [[nodiscard]] std::span<std::byte> span() noexcept { return bytes_; }
            [[nodiscard]] std::span<const std::byte> span() const noexcept { return bytes_; }
            [[nodiscard]] iterator begin() noexcept { return bytes_.data(); }
            [[nodiscard]] const_iterator begin() const noexcept { return bytes_.data(); }
            [[nodiscard]] iterator end() noexcept {
                return bytes_.empty() ? bytes_.data() : bytes_.data() + bytes_.size();
            }

            [[nodiscard]] const_iterator end() const noexcept {
                return bytes_.empty() ? bytes_.data() : bytes_.data() + bytes_.size();
            }

            [[nodiscard]] value_type &operator[](size_type index) noexcept { return bytes_[index]; }
            [[nodiscard]] const value_type &operator[](size_type index) const noexcept { return bytes_[index]; }

            void reserve(size_type requested_capacity) { bytes_.reserve(requested_capacity); }
            void resize(size_type size, std::byte value = {}) { bytes_.resize(size, value); }
            void clear() noexcept { bytes_.clear(); }
            void push_back(std::byte value) { bytes_.push_back(value); }

            friend bool operator==(const basic_byte_buffer &, const basic_byte_buffer &) = default;

        private:
            //TODO dont use vector, hand write byte buffer so support resize_and_overwrite and resize_uninitialized member function
            std::vector<std::byte, allocator_type> bytes_{};
        };

        using byte_buffer = basic_byte_buffer<>;
    }

    using byte_buffer = storage::byte_buffer;
}
