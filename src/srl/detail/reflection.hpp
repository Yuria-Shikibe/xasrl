#pragma once

#include <cstdint>
#include <meta>

#include "reflection_facts.hpp"

namespace srl::detail {
    enum class reflected_entity_kind : std::uint8_t {
        member = 1,
        base = 2,
    };

    template<std::meta::info Entity, reflected_entity_kind Kind, typename Object>
    [[nodiscard]] constexpr decltype(auto) entity_reference(Object &object) noexcept {
        // Splice the specific subobject, not just its type: another base may
        // contain a second subobject of the same type.
        return (object.[:Entity:]);
    }

}
