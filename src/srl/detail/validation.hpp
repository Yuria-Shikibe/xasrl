#pragma once

#include <meta>

#include "reflection_facts.hpp"

namespace xasrl::detail {
    enum class validation_mode { checked, unchecked };

    consteval validation_mode validation_for(std::meta::info entity, validation_mode inherited = validation_mode::checked) {
        return annotation_count(entity, ^^tag::bypass_validation) != 0 ? validation_mode::unchecked : inherited;
    }
}
