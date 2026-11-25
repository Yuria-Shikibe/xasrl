#pragma once

#include <cstdint>
#include <meta>

#include "annotation_target.hpp"

namespace srl {
    class serialization_context;

    namespace detail {
        template<typename T>
        /*constexpr ?*/inline unsigned char identity_token{};
    }

    template<typename Tag>
    [[nodiscard]] constexpr const void *handler_key() noexcept {
        return &detail::identity_token<typename[:std::meta::remove_cvref(^^Tag):]>;
    }

    struct [[=tag::target::member_field]] conversion_tag {
        std::meta::info target_type{};
        const void *handler_id{};
    };

    enum class duplicate_policy : std::uint8_t {
        deduplicate,
        reject,
    };

}
