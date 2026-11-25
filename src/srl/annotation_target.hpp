#pragma once

#include <cstdint>

namespace srl::tag{
    // Reflectable placement hints; schema rules still decide whether a use is valid.
    enum class target : std::uint8_t {
        member_field = 1 << 0,
        compound = 1 << 1,
        enumerator = 1 << 2,
        base_class = 1 << 3,
        enum_type = 1 << 4,

        data = base_class | member_field
    };

    constexpr target operator|(target lhs, target rhs) noexcept {
        return static_cast<target>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
    }

    constexpr target operator&(target lhs, target rhs) noexcept {
        return static_cast<target>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
    }
}
