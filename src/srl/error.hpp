#pragma once

#include <string_view>

#include "types.hpp"
#include "enum.hpp"

namespace xasrl {
    [[nodiscard]] constexpr std::string_view message(error_code code) noexcept {
        const auto name = enum_name(code);
        return name.empty() ? std::string_view{"unknown_error_code"} : name;
    }

}
