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
        // Array elements share one reflected layout. Offsets in children are
        // relative to an element; only the repeat count scales with array size.
        const object_field *element_fields{};
        std::size_t element_field_count{};
        szt element_count{};
        szt stride{};
        szt payload_size{};

        [[nodiscard]] consteval bool repeated() const noexcept { return element_count != 0; }
        [[nodiscard]] consteval std::span<const object_field> children() const noexcept {
            return {element_fields, element_field_count};
        }
    };

    struct object_layout {
        szt payload_size{};

        // Public structural members are required by define_static_array.
        const object_field *field_data{};
        std::size_t field_count{};

        [[nodiscard]] consteval std::span<const object_field> fields() const noexcept {
            return {field_data, field_count};
        }
    };

}
