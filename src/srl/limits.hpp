#pragma once

#include "types.hpp"

namespace xasrl {
    struct decode_limits {
        szt max_input_bytes = 256ull * 1024 * 1024;
    };
}
