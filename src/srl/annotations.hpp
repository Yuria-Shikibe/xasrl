#pragma once

#include <compare>

#include "context.hpp"
#include "wire.hpp"

namespace srl {
    namespace tag {
        struct field {
            id_type id{};

            constexpr auto operator<=>(const field &) const noexcept = default;
        };

        struct atomic {
            bool allow_padding{false};
        };

        struct auto_seq {};

        struct bypass_pointer_check {};

        struct transient {};

        struct container {
            duplicate_policy duplicates{duplicate_policy::deduplicate};
        };
    }
}
