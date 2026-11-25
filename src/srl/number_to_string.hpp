#pragma once

#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <limits>
#include <string_view>
#include <type_traits>

#include "smallest_uint.hpp"

namespace xasrl::detail {
    template<unsigned Base, std::unsigned_integral T>
        requires (Base >= 2 && Base <= 36)
    [[nodiscard]] constexpr std::size_t unsigned_digit_count(T value) noexcept {
        std::size_t count = 1;
        while (value >= Base) {
            value /= Base;
            ++count;
        }
        return count;
    }

    template<unsigned Base, typename Char, std::integral T>
    class integer_text {
        static constexpr auto capacity = [] {
            if constexpr (std::signed_integral<T>) {
                using unsigned_type = std::make_unsigned_t<T>;
                constexpr auto minimum = static_cast<unsigned_type>((std::numeric_limits<T>::min)());
                constexpr auto magnitude = static_cast<unsigned_type>(unsigned_type{0} - minimum);
                return detail::unsigned_digit_count<Base>(magnitude) + 1;
            } else {
                return detail::unsigned_digit_count<Base>((std::numeric_limits<T>::max)());
            }
        }();

    public:
        using buffer_type = std::array<Char, capacity>;
        using view_type = std::basic_string_view<Char>;
        using size_type = smallest_uint_t<capacity>;

        constexpr explicit integer_text(T value) noexcept {
            // Capacity covers every value of T in Base. to_chars only writes char.
            if constexpr (std::same_as<Char, char>) {
                const auto first = buffer_.data();
                const auto result = std::to_chars(first, first + capacity, value, Base);
                size_ = static_cast<size_type>(result.ptr - first);
            } else {
                std::array<char, capacity> narrow{};
                const auto first = narrow.data();
                const auto result = std::to_chars(first, first + capacity, value, Base);
                size_ = static_cast<size_type>(result.ptr - first);
                for (std::size_t i = 0; i < size_; ++i) {
                    buffer_[i] = static_cast<Char>(narrow[i]);
                }
            }
        }

        [[nodiscard]] constexpr const buffer_type &buffer() const & noexcept { return buffer_; }
        constexpr const buffer_type &buffer() const && = delete;

        // Form the view on demand so copies and moves refer to their own buffer.
        [[nodiscard]] constexpr view_type view() const & noexcept {
            return {buffer_.data(), size_};
        }

        constexpr view_type view() const && = delete;
        [[nodiscard]] constexpr size_type size() const noexcept {
            return size_;
        }

        constexpr explicit(false) operator view_type() const & noexcept {
            return view();
        }

        //TODO support cstr?

    private:

        buffer_type buffer_{};
        size_type size_{};
    };
}



namespace xasrl {
    // The buffer holds the longest spelling for T in Base, without a terminator.
    // Read the actual characters through view() while the result is alive.
    template<unsigned Base = 10, typename Char = char, std::integral T>
        requires (Base >= 2 && Base <= 36 && !std::same_as<T, bool>
                  && (std::same_as<Char, char> || std::same_as<Char, wchar_t> || std::same_as<Char, char8_t> || std::same_as<Char, char16_t> || std::same_as<Char, char32_t>))
    [[nodiscard]] constexpr auto number_to_string(T value) noexcept {
        return detail::integer_text<Base, Char, T>{value};
    }

}
