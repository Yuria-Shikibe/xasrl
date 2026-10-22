#pragma once

#include "types.hpp"

namespace srl {
    struct decode_limits {
        szt max_input_bytes = 256ull * 1024 * 1024;
    };
}
