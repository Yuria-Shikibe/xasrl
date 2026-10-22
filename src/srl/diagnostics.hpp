#pragma once

#include <string>
#include <string_view>

#include "error.hpp"

namespace srl {
    // A field ID alone is not a full path: it can belong to a nested record or
    // a container slot. Accept a caller-known path rather than guessing one.
    [[nodiscard]] inline std::string format_error(const error &failure, std::string_view field_path = {}) {
        std::string result{message(failure.code)};
        if (!failure) { return result; }
        if (!field_path.empty()) {
            result.append(" path=").append(field_path);
        }
        if (failure.id) {
            result.append(" id=").append(std::to_string(*failure.id));
        }
        result.append(" offset=").append(std::to_string(failure.offset));
        if (failure.system_error) {
            const auto &system = *failure.system_error;
            result.append(" system=").append(system.category().name()).append(":")
                .append(std::to_string(system.value())).append(" (").append(system.message()).append(")");
        }
        return result;
    }
}
