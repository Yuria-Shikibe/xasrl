#pragma once

#include <string>
#include <string_view>

#include "error.hpp"
#include "number_to_string.hpp"

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
            const auto id = number_to_string(*failure.id);
            result.append(" id=").append(id.view());
        }
        const auto offset = number_to_string(failure.offset);
        result.append(" offset=").append(offset.view());
        if (failure.system_error) {
            const auto &system = *failure.system_error;
            const auto code = number_to_string(system.value());
            result.append(" system=").append(system.category().name()).append(":")
                .append(code.view()).append(" (").append(system.message()).append(")");
        }
        return result;
    }
}
