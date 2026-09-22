#pragma once

#include <compare>

#include "container.hpp"
#include "wire.hpp"

namespace srl {
    namespace tag {
        struct field {
            id_type id{};

            constexpr auto operator<=>(const field &) const noexcept = default;
        };

        struct atomic {
            bool propagate{true};
            bool allow_padding{false};
            bool bypass_pointer_check{false};
        };

        struct container {
            duplicate_policy duplicates{duplicate_policy::deduplicate};
        };
    }
}

