#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <meta>

#include "conversion.hpp"
#include "types.hpp"

namespace srl {
    namespace defaults {
        // An explicit fallback<> uses T{}. Unannotated fields are required.
        // This does not re-evaluate the enclosing class's member initializer.
        struct initialized {};

        //TODO support uninitialized ?

        // Value must be a template argument; T{Value} rejects narrowing conversions.
        template<auto Value>
        struct value {};

        // void(T&): modifies the existing field, without a preliminary reset.
        template<auto Function>
        struct modifier {};

        // Exactly one of T(), T(const serialization_context&), void(T&), or
        // void(T&, const serialization_context&). Invoked only for an absent ID.
        template<auto Function>
        struct provider {};
    }

    namespace detail {
        template<typename T>
        concept initialized_assignable = requires(T &out) { out = T{}; };

        consteval bool initialized_fallback_valid(std::meta::info type) {
            while (std::meta::is_bounded_array_type(type)) { type = std::meta::remove_extent(type); }
            return std::meta::extract<bool>(std::meta::substitute(^^initialized_assignable, {type}));
        }

        template<typename Policy>
        struct fallback_policy;

        template<>
        struct fallback_policy<defaults::initialized> {
            template<typename T>
            static constexpr bool valid = initialized_fallback_valid(^^T);

            template<typename T>
                requires valid<T>
            static constexpr void apply(T &out, const serialization_context &context) {
                if constexpr (std::meta::is_bounded_array_type(^^T)) {
                    for (auto &element: out) {
                        fallback_policy::apply(element, context);
                    }
                } else {
                    out = T{};
                }
            }
        };

        template<auto Value>
        struct fallback_policy<defaults::value<Value>> {
            template<typename T>
            static constexpr bool valid = requires(T &out) { out = T{Value}; };

            template<typename T>
                requires valid<T>
            static constexpr void apply(T &out, const serialization_context &) {
                out = T{Value};
            }
        };

        template<auto Function>
        struct fallback_policy<defaults::modifier<Function>> {
            template<typename T>
            static constexpr bool valid = requires(T &out) {
                { std::invoke(Function, out) } -> std::same_as<void>;
            };

            template<typename T>
                requires valid<T>
            static constexpr void apply(T &out, const serialization_context &) {
                std::invoke(Function, out);
            }
        };

        template<auto Function>
        struct fallback_policy<defaults::provider<Function>> {
            // Count invocable shapes before checking results: no overload priority.
            template<typename T>
            static constexpr int signatures = std::invocable<decltype((Function))>
                + std::invocable<decltype((Function)), const serialization_context &>
                + std::invocable<decltype((Function)), T &>
                + std::invocable<decltype((Function)), T &, const serialization_context &>;

            template<typename T>
            static constexpr bool valid = signatures<T> == 1 && (
                requires(T &out) {
                    { std::invoke(Function) } -> std::same_as<T>;
                    out = std::invoke(Function);
                } || requires(T &out, const serialization_context &ctx) {
                    { std::invoke(Function, ctx) } -> std::same_as<T>;
                    out = std::invoke(Function, ctx);
                } || requires(T &out) {
                    { std::invoke(Function, out) } -> std::same_as<void>;
                } || requires(T &out, const serialization_context &ctx) {
                    { std::invoke(Function, out, ctx) } -> std::same_as<void>;
                });

            template<typename T>
                requires valid<T>
            static constexpr void apply(T &out, const serialization_context &ctx) {
                if constexpr (std::invocable<decltype((Function))>) {
                    out = std::invoke(Function);
                } else if constexpr (std::invocable<decltype((Function)), const serialization_context &>) {
                    out = std::invoke(Function, ctx);
                } else if constexpr (std::invocable<decltype((Function)), T &>) {
                    std::invoke(Function, out);
                } else {
                    std::invoke(Function, out, ctx);
                }
            }
        };

        template<typename Policy>
        concept default_policy = requires { sizeof(fallback_policy<Policy>); };
    }

    namespace tag {
        // [[=srl::tag::fallback{srl::defaults::value<42>{}}]]
        // One policy per annotation; field-level validation rejects duplicates.
        // Applies to the memory type before any storage conversion. Streams and
        // members inside leaf objects have no independently absent IDs and
        // reject this tag.
        template<detail::default_policy Policy = defaults::initialized>
        struct fallback {
            constexpr explicit fallback(Policy = {}) {}

            template<typename T>
            static constexpr bool valid_for = detail::fallback_policy<Policy>::template valid<T>;

            template<typename T>
                requires valid_for<T>
            static constexpr void apply(T &out, const serialization_context &context) {
                detail::fallback_policy<Policy>::apply(out, context);
            }
        };

        struct field {
            id_type id{};

            constexpr auto operator<=>(const field &) const noexcept = default;
            constexpr bool operator==(const field &) const noexcept = default;
        };

        struct leaf {
            bool allow_padding{false};
        };

        // Every subset of mask, including zero, is a valid enum value.
        struct flags {
            // Zero infers the mask by OR-ing all enumerators; the result must be nonzero.
            std::uint64_t mask{};
        };

        //TODO
        struct bypass_validation{};

        struct auto_seq {};

        struct auto_stream {};

        struct bypass_pointer_check {};

        struct transient {};

        struct container {
            duplicate_policy duplicates{duplicate_policy::deduplicate};
        };
    }

    namespace detail {
        consteval bool is_fallback_annotation(std::meta::info annotation) {
            const auto type = std::meta::remove_cvref(std::meta::type_of(annotation));
            return std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^tag::fallback;
        }

        consteval std::size_t fallback_annotation_count(std::meta::info entity) {
            std::size_t count{};
            for (auto annotation: std::meta::annotations_of(entity)) {
                count += is_fallback_annotation(annotation);
            }
            return count;
        }

        consteval std::meta::info fallback_annotation_type(std::meta::info entity) {
            for (auto annotation: std::meta::annotations_of(entity)) {
                if (is_fallback_annotation(annotation)) {
                    return std::meta::remove_cvref(std::meta::type_of(annotation));
                }
            }
            return ^^tag::fallback<>;
        }

        template<typename Annotation, typename T>
        inline constexpr bool fallback_valid_v = Annotation::template valid_for<T>;

        consteval bool fallback_valid(std::meta::info entity) {
            return fallback_annotation_count(entity) <= 1
                && std::meta::extract<bool>(std::meta::substitute(^^fallback_valid_v,
                    {fallback_annotation_type(entity), std::meta::type_of(entity)}));
        }
    }
}
