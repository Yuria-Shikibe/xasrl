#pragma once

#include <compare>
#include <variant>

#include "context.hpp"
#include "wire.hpp"

namespace srl {
    namespace tag {
        enum struct fallback_type {
            def,
            ban,
            copy,
            prov,
        };

        // template <fallback_type type, auto Prov = std::monostate{}>
        // struct fallback {
        //     static_assert((type == fallback_type::def || type == fallback_type::ban) || !std::meta::is_same_type(std::meta::type_of(^^Prov), ^^std::monostate));
        //
        //     constexpr static auto& provider = Prov;
        //
        //     template <typename T>
        //     static constexpr T get(){
        //         static_assert(type != fallback_type::ban, "fallback on such type is banned");
        //
        //         if constexpr (type == fallback_type::def) {
        //             return T{};
        //         }else if constexpr (type == fallback_type::copy) {
        //             static_assert(std::meta::is_convertible_type(^^T, std::meta::type_of(^^Prov)));
        //             return Prov;
        //         }else {
        //             return std::invoke_r<T>(Prov);
        //         }
        //     }
        // };

        struct field {
            id_type id{};

            constexpr auto operator<=>(const field &) const noexcept = default;
            constexpr bool operator==(const field &) const noexcept = default;
        };

        struct atomic {
            bool allow_padding{false};
        };

        struct auto_seq {};
        struct auto_stream {};

        struct bypass_pointer_check {};

        struct transient {};

        struct container {
            duplicate_policy duplicates{duplicate_policy::deduplicate};
        };
    }
}
