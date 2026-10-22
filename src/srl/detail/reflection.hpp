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
        if constexpr (Kind == reflected_entity_kind::member) {
            return (object.[:Entity:]);
        } else {
            using base_type = typename[:std::meta::remove_cvref(std::meta::type_of(Entity)):];
            if constexpr (std::meta::is_const_type(^^Object)) {
                return (const base_type &)object;
            } else {
                return (base_type &)object;
            }
        }
    }

}
