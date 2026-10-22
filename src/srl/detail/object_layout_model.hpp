#pragma once

#include <cstddef>
#include <meta>
#include <span>

#include "../types.hpp"
#include "validation.hpp"

namespace srl::detail {
    struct object_field {
        std::meta::info type{};
        std::meta::info entity{};
        szt object_offset{};
        szt wire_offset{};
        szt size{};
        validation_mode validation = validation_mode::checked;
    };

    struct object_layout {
        szt payload_size{};

        //TODO directly store it as span
        const object_field *field_data{};
        std::size_t field_count{};

        [[nodiscard]] consteval std::span<const object_field> fields() const noexcept {
            return {field_data, field_count};
        }
    };

}
