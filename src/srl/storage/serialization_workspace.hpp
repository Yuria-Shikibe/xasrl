#pragma once

#include <array>
#include <cstddef>
#include <memory_resource>

namespace srl::storage {
    // Scratch storage shared by all runtime preparation plans in one encode operation.
    // The resource is intentionally operation-local so it can be reset and reused without
    // exposing allocator state through serialization_context.
    class serialization_workspace {
        static constexpr std::size_t initial_size = 4096;

        std::array<std::byte, initial_size> initial_;
        std::pmr::monotonic_buffer_resource resource_{
            initial_.data(), initial_.size(), std::pmr::new_delete_resource()};

    public:
        serialization_workspace() noexcept {}
        serialization_workspace(const serialization_workspace &) = delete;
        serialization_workspace &operator=(const serialization_workspace &) = delete;

        [[nodiscard]] std::pmr::memory_resource *resource() noexcept { return &resource_; }

        void reset() noexcept { resource_.release(); }
    };
}

namespace srl {
    using serialization_workspace = storage::serialization_workspace;
}

namespace srl::detail {
    struct no_serialization_workspace {
        [[nodiscard]] static std::pmr::memory_resource *resource() noexcept { return nullptr; }
    };
}
