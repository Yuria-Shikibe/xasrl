#pragma once

#include <cstdint>
#include <meta>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../number_to_string.hpp"

namespace xasrl {
    enum class schema_error : std::uint8_t {
        none, invalid_schema, unsupported_type, conflicting_annotations, duplicate_field_id,
        field_id_overflow, recursive_type, invalid_conversion, invalid_object_representation,
        extent_overflow, mapping_limit, missing_default_constructor, unsupported_entity,
        invalid_fallback, invalid_annotation_target,
    };

    struct schema_issue {
        schema_error reason{};
        std::meta::info entity{};
        std::span<const std::meta::info> path;
        std::string_view detail;
    };

    [[nodiscard]] consteval std::string format_schema_issue(const schema_issue &issue) {
        std::string result = "srl: [";

        //TODO reuse the enum to string
        for (auto value: std::meta::enumerators_of(^^schema_error)) {
            if (issue.reason == std::meta::extract<schema_error>(std::meta::constant_of(value))) {
                result.append(std::meta::identifier_of(value));

            }
        }

        result.push_back(']');

        for (auto entity: issue.path) {
            result.append(" -> ").append(std::meta::display_string_of(entity));
        }
        if (!issue.detail.empty()) { result.append(": ").append(issue.detail); }
        return result;
    }
}

namespace xasrl::detail {
    class schema_failure : public std::runtime_error {
        schema_issue issue_;
    public:
        consteval explicit schema_failure(schema_issue issue)
            : std::runtime_error(format_schema_issue(issue)), issue_(issue) {}

        [[nodiscard]] consteval const schema_issue &issue() const noexcept { return issue_; }
    };

    [[noreturn]] consteval void reject_schema(schema_error reason,
        std::vector<std::meta::info> path, std::meta::info entity = {},
        std::string_view detail = {}) {
        if (entity != std::meta::info{} && (path.empty() || path.back() != entity)) {
            path.push_back(entity);
        }
        throw schema_failure({reason, entity, std::define_static_array(path),
            std::define_static_string(detail)});
    }
}
