#pragma once

#include <concepts>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

#include "model.hpp"

namespace srl::detail {
    consteval type_hash_type hash_integer(type_hash_type hash, szt value) {
        for (unsigned shift = 0; shift != 64; shift += 8) {
            hash = hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
        }
        return hash;
    }

    template<typename T>
    inline constexpr type_hash_type scalar_wire_id = [] consteval {
        if constexpr (std::same_as<T, bool>) {
            return 0x100u;
        } else if constexpr (std::integral<T>) {
            return (std::is_signed_v<T> ? 0x200u : 0x300u) + sizeof(T);
        } else if constexpr (std::floating_point<T>) {
            return 0x400u + sizeof(T);
        } else {
            // Enum and custom codec identities are nominal; aliases are dealiased.
            return hash_integer(type_hash<T>(), sizeof(T));
        }
    }();

    consteval type_hash_type scalar_wire_type(std::meta::info type) {
        return std::meta::extract<type_hash_type>(std::meta::substitute(^^scalar_wire_id, {type}));
    }

    consteval type_hash_type atomic_wire_type(const schema_node &node) {
        auto hash = hash_integer(scalar_wire_type(node.type), node.encode_extent);
        for (const auto field: node.object.fields()) {
            hash = hash_integer(hash, scalar_wire_type(field.type));
            hash = hash_integer(hash, field.wire_offset);
            hash = hash_integer(hash, field.size);
            if (field.entity != std::meta::info{} && !std::meta::is_base(field.entity)) {
                hash = hash_string(hash, std::meta::identifier_of(field.entity));
            }
        }
        return hash;
    }
}
