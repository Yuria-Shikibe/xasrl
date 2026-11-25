#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace srl {
    // Max is inclusive. Select the narrowest standard fixed-width unsigned type.
    template<std::uint64_t Max>
    using smallest_uint_t =
        std::conditional_t<(Max <= (std::numeric_limits<std::uint8_t>::max)()), std::uint8_t,
        std::conditional_t<(Max <= (std::numeric_limits<std::uint16_t>::max)()), std::uint16_t,
        std::conditional_t<(Max <= (std::numeric_limits<std::uint32_t>::max)()), std::uint32_t,
            std::uint64_t>>>;
}
