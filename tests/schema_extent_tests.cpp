#include "srl/schema/extent.hpp"

#include <array>
#include <cstddef>
#include <meta>
#include <span>
#include <string_view>
#include <type_traits>

namespace {
    using extent = srl::schema_extent;
    constexpr auto maximum = srl::detail::maximum_entry_size;
    constexpr auto zero = extent::fixed(0);
    constexpr auto one = extent::fixed(1);
    constexpr auto dynamic = extent::dynamic();
    constexpr auto limit = extent::fixed(maximum);

    static_assert(!std::is_aggregate_v<extent>);
    static_assert(!std::is_default_constructible_v<extent>);
    static_assert(!std::is_constructible_v<extent, srl::szt>);
    static_assert(!std::is_convertible_v<extent, srl::szt>);
    static_assert(std::is_const_v<decltype(extent::bytes)>);
    static_assert(dynamic.is_dynamic() && !limit.is_dynamic());
    static_assert(dynamic.span_extent() == std::dynamic_extent);
    static_assert(limit.span_extent() == maximum);
    static_assert(limit + zero == limit && zero + limit == limit);
    static_assert(limit + dynamic == dynamic && dynamic + limit == dynamic);
    static_assert(dynamic + dynamic == dynamic);
    static_assert(zero.scaled_by(7) == zero);
    static_assert(dynamic.scaled_by(0) == zero);
    static_assert(dynamic.scaled_by(1) == dynamic);
    static_assert(limit.scaled_by(1) == limit);
    static_assert(extent::fixed(maximum / 2).scaled_by(2) == limit);
    static_assert(zero.scaled_by_dynamic_count() == zero);
    static_assert(one.scaled_by_dynamic_count() == dynamic);
    static_assert(dynamic.scaled_by_dynamic_count() == dynamic);

    consteval bool fails(auto operation, std::string_view reason) {
        try { operation(); }
        catch (const srl::detail::extent_overflow &failure) {
            const std::string_view message{failure.what()};
            return message.contains(reason) && message.contains("maximum_entry_size=");
        }
        return false;
    }

    static_assert(fails([] { (void)extent::fixed(std::dynamic_extent); }, "fixed size out of range"));
    static_assert(fails([] { (void)(limit + one); }, "addition overflow"));
    static_assert(fails([] { (void)extent::fixed(maximum / 2 + 1).scaled_by(2); }, "multiplication overflow"));
    // A numeric count never uses the span sentinel convention.
    static_assert(fails([] { (void)one.scaled_by(std::dynamic_extent); }, "multiplication overflow"));
    static_assert(zero.scaled_by(std::dynamic_extent) == zero);

    static_assert([] consteval {
        auto value = limit;
        try { value += one; }
        catch (const srl::detail::extent_overflow &) {
            return value == limit;
        }
        return false;
    }());
    static_assert([] consteval {
        auto value = one;
        value = value;
        value += value;
        if (value != extent::fixed(2)) { return false; }
        value = dynamic;
        value = zero;
        return value == zero;
    }());

    // Metadata containing extents must still support reflection promotion.
    constexpr auto stored = std::define_static_array(std::array{zero, limit, dynamic});
    static_assert(stored[0] == zero && stored[1] == limit && stored[2] == dynamic);
}
