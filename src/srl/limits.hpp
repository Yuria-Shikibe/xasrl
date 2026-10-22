#pragma once

#include "types.hpp"

namespace srl {
    struct decode_limits {
        szt max_input_bytes = 256ull * 1024 * 1024;
        szt max_container_elements = 1024ull * 1024;
        szt max_total_elements = 4ull * 1024 * 1024;
        // Sum of count * sizeof(element_type) for visited containers. This
        // bounds element storage, not allocator bookkeeping or user callbacks.
        szt max_element_storage = 512ull * 1024 * 1024;
    };

    namespace detail {
        class decode_budget {
            const decode_limits &limits_;
            szt elements_left_;
            szt storage_left_;

        public:
            explicit decode_budget(const decode_limits &limits) noexcept
                : limits_(limits), elements_left_(limits.max_total_elements),
                  storage_left_(limits.max_element_storage) {}

            [[nodiscard]] bool consume(szt count, szt element_size) noexcept {
                if (count > limits_.max_container_elements || count > elements_left_
                    || count > storage_left_ / element_size) {
                    return false;
                }
                elements_left_ -= count;
                storage_left_ -= count * element_size;
                return true;
            }
        };
    }
}
