#pragma once

#include <concepts>
#include <cstdint>
#include <meta>
#include <utility>

#include "model.hpp"
#include "../codec/base.hpp"

namespace srl::detail {
    consteval type_hash_type hash_integer(type_hash_type hash, szt value) {
        for (unsigned shift = 0; shift != 64; shift += 8) {
            hash = hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
        }
        return hash;
    }

    consteval type_hash_type scalar_wire_type(std::meta::info type) {
        type = recursively_dealias(type);
        if (type == ^^bool) {
            return 0x100u;
        }
        if (std::meta::is_integral_type(type)) {
            return (std::meta::is_signed_type(type) ? 0x200u : 0x300u) + std::meta::size_of(type);
        }
        if (std::meta::is_floating_point_type(type)) {
            return 0x400u + std::meta::size_of(type);
        }
        auto hash = hash_integer(type_hash(type), std::meta::size_of(type));
        if (std::meta::is_enum_type(type)) {
            const auto policy = make_enum_policy(type);
            if (policy.flags) {
                hash = hash_integer(hash_integer(hash, 0x666c616773ull), policy.mask);
            }
        }
        return hash;
    }

    consteval type_hash_type leaf_wire_type(const schema_node &node) {
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
