#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "types.hpp"

namespace srl::detail {
        constexpr std::uint64_t storage_magic = 0x31304e49424c5253ull;
        constexpr std::uint32_t storage_version = 5;
        constexpr std::uint32_t compression_none = 0;
        constexpr szt storage_head_wire_size = sizeof(storage_head);
        constexpr szt entry_head_wire_size = sizeof(entry_head);
        // The all-ones extent is reserved by std::span for dynamic_extent.
        static_assert(sizeof(std::size_t) <= sizeof(szt));
        constexpr szt maximum_entry_size = std::numeric_limits<decltype(std::dynamic_extent)>::max() - 1;
}
