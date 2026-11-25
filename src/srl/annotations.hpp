#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <meta>
#include <stdexcept>

#include "annotation_target.hpp"
#include "conversion.hpp"
#include "detail/default_snapshot.hpp"
#include "types.hpp"

namespace xasrl {

    namespace defaults {
        /**
         * Value-initializes the field with `T{}` (element-wise for arrays).
         * This is the policy of `tag::fallback{}`; it does not use the owner's initializer.
         */
        struct initialized {};

        /**
         * Restores the field value captured from `Owner{}` at compile time.
         * If the snapshot cannot be captured, the schema is rejected.
         */
        struct automatic {};

        /** Assigns `T{Value}` from a compile-time constant, rejecting narrowing conversions. */
        template<auto Value>
        struct value {};

        /** Invokes `void(T&)` on the existing field value without resetting it first. */
        template<auto Function>
        struct modifier {};

        /**
         * Computes a replacement value or modifies the existing field in place.
         * Accepts exactly one of `T()`, `T(const serialization_context&)`, `void(T&)`,
         * or `void(T&, const serialization_context&)`.
         */
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
        struct fallback_policy<defaults::automatic> {
            // This policy requires a reflected entity, not just its value type.
            template<typename T>
            static constexpr bool valid = false;
        };

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

        struct [[=target::compound | target::data]] inline_mapping {};

        /**
         * Makes one record field optional and applies `Policy` to its in-memory value
         * only when its ID is absent, not when the encoded value is invalid.
         * Stream entries and members inside leaf objects have no independent field IDs.
         */
        template<detail::default_policy Policy = defaults::initialized>
        struct [[=target::data]] fallback {
            constexpr explicit fallback(Policy = {}) {}

            template<typename T>
            static constexpr bool valid_for = detail::fallback_policy<Policy>::template valid<T>;

            template<typename T>
                requires valid_for<T>
            static constexpr void apply(T &out, const serialization_context &context) {
                detail::fallback_policy<Policy>::apply(out, context);
            }
        };

        /** Shorthand for `fallback{defaults::automatic{}}`. */
        constexpr inline fallback use_initializer{defaults::automatic{}};

        /**
         * Includes a member or direct base in a record under an explicit wire ID.
         * Overrides the current ID in `auto_seq`; ignored inside `auto_stream`.
         */
        struct [[=target::data]] field {
            id_type id{};

            constexpr auto operator<=>(const field &) const noexcept = default;
            constexpr bool operator==(const field &) const noexcept = default;
        };

        struct [[=target::compound]] extractor {};

        /** Adds an incoming ID for in-memory assignment only. May be repeated. */
        struct [[=target::data]] accept_id {
            id_type id{};
        };

        /** Selects an exact source member or direct base for in-memory assignment. May be repeated. */
        struct [[=target::data]] accept_member {
            const std::meta::info where;

            consteval explicit accept_member(std::meta::info where)
                : where(where) {
                if (!std::meta::is_base(where)
                    && (!std::meta::is_nonstatic_data_member(where) || std::meta::is_bit_field(where))) {
                    throw std::invalid_argument("accept_member requires a non-bit-field member or a base");
                }
            }

            consteval accept_member(std::meta::info class_type, std::meta::info base_type)
                : where(find_base(class_type, base_type)) {}

        private:
            static consteval std::meta::info find_base(std::meta::info owner, std::meta::info base) {
                if (std::meta::is_class_type(owner) && std::meta::is_complete_type(owner)
                    && std::meta::is_class_type(base)) {
                    for (auto candidate: std::meta::bases_of(owner, std::meta::access_context::unchecked())) {
                        if (std::meta::is_same_type(std::meta::type_of(candidate), base)) { return candidate; }
                    }
                }
                throw std::invalid_argument("accept_member requires a direct base of the specified class");
            }
        };

        /**
         * Encodes a type as one fixed-size payload, without nested entries.
         * On a type, enables automatic object layout or a custom leaf codec; on a
         * member or base, forces object-representation encoding for that field only.
         */
        struct [[=target::compound | target::data]] leaf {
            /** Allows non-unique object representations; encoded padding is still zeroed. */
            bool allow_non_unique_presentation{false};
        };


        /**
         * Builds a record with IDs starting at zero, visiting direct bases before members
         * in declaration order. Skips `transient`; an explicit `field` ID resets the sequence.
         */
        struct [[=target::compound]] auto_seq {};

        /**
         * Encodes non-transient direct bases and members in declaration order, without
         * record field IDs or an offset table. On a member or base, overrides that subobject only.
         */
        struct [[=target::compound | target::data]] auto_stream {};

        /**
         * Excludes a field from `auto_seq`, `auto_stream` or an automatic leaf layout.
         * Decoding leaves the excluded value unchanged; leaf layouts allow this only on members.
         */
        struct [[=target::data]] transient {};

        /**
         * Overrides the context's duplicate policy for a container field on decode.
         * For converted fields, applies to the storage container type.
         */
        struct [[=target::data]] container {
            /** Deduplicate using the container's semantics, or reject duplicate keys or elements. */
            duplicate_policy duplicates{duplicate_policy::deduplicate};
        };

        /**
         * Permits raw pointer and member-pointer representations in object-layout encoding.
         * On a type, applies to its direct bases and members; on a field, applies only there.
         * It does not make addresses portable or valid across processes.
         */
        struct [[=target::compound | target::data]] bypass_pointer_check {};

        /**
         * Skips built-in bool, enum and flag value checks in the annotated serialized subtree.
         * The caller must supply valid C++ representations; structural and custom codec checks still run.
         */
        struct [[=target::compound | target::enum_type | target::data]] bypass_validation {};


        /** Treats an enum as flags: any subset of the allowed bits, including zero, is valid. */
        struct [[=target::enum_type]] bit_flag {
            /** A zero mask infers the allowed bits by OR-ing all enumerators; the result must be nonzero. */
            std::uint64_t mask{};
        };

    }

    namespace detail {
        template<std::meta::info Entity>
        using fallback_owner = [:std::meta::parent_of(Entity):];

        template<std::meta::info Entity>
        using fallback_value = [:std::meta::remove_cvref(std::meta::type_of(Entity)):];

        template<std::meta::info Entity>
        consteval auto capture_field_default() {
            fallback_owner<Entity> object{};
            using snapshot = default_snapshot<fallback_value<Entity>>;
            if constexpr (std::meta::is_base(Entity)) {
                return snapshot::capture((const fallback_value<Entity> &)object);
            } else {
                return snapshot::capture(object.[:Entity:]);
            }
        }

        template<auto>
        struct captured_default {};

        // Requiring the complete snapshot as a template argument also rejects
        // uninitialized values and pointers into the temporary default object.
        template<std::meta::info Entity>
        concept automatic_fallback_valid =
            (std::meta::is_nonstatic_data_member(Entity) || std::meta::is_base(Entity))
            && !std::meta::is_reference_type(std::meta::type_of(Entity))
            && !std::meta::is_const_type(std::meta::type_of(Entity))
            && !std::meta::is_volatile_type(std::meta::type_of(Entity))
            && requires { fallback_owner<Entity>{}; }
            && default_snapshot<fallback_value<Entity>>::valid
            && requires { typename captured_default<capture_field_default<Entity>()>; };

        template<std::meta::info Entity>
            requires automatic_fallback_valid<Entity>
        struct automatic_fallback {
            static constexpr auto value = capture_field_default<Entity>();

            static void apply(fallback_value<Entity> &out, const serialization_context &) {
                default_snapshot<fallback_value<Entity>>::apply(out, value);
            }
        };

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
            if (fallback_annotation_count(entity) > 1) { return false; }
            const auto annotation = fallback_annotation_type(entity);
            if (annotation == ^^tag::fallback<defaults::automatic>) {
                return std::meta::extract<bool>(std::meta::substitute(^^automatic_fallback_valid,
                    {std::meta::reflect_constant(entity)}));
            }
            return std::meta::extract<bool>(std::meta::substitute(^^fallback_valid_v,
                {annotation, std::meta::type_of(entity)}));
        }

        // Resolve policies needing the declaring class once, while building the
        // schema. Decoding only sees the captured field value, never an Owner{}.
        consteval std::meta::info resolved_fallback_type(std::meta::info entity) {
            const auto annotation = fallback_annotation_type(entity);
            if (annotation == ^^tag::fallback<defaults::automatic>) {
                return std::meta::substitute(^^automatic_fallback, {std::meta::reflect_constant(entity)});
            }
            return annotation;
        }
    }
}
