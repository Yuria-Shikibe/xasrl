#pragma once

#include <array>
#include <cstddef>
#include <meta>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>

#include "../schema.hpp"

namespace xasrl::mem {
    namespace detail {
        consteval std::optional<std::size_t> find_field(const schema_tree &tree, id_type id) {
            for (auto index: tree.child_indices(tree.value_index())) {
                const auto candidate = tree[index].record_field().id;
                if (candidate == id) { return index; }
                if (candidate > id) { break; }
            }
            return std::nullopt;
        }

        struct field_match {
            std::optional<std::size_t> index{};
            bool ambiguous{};
        };

        consteval field_match match_field(const schema_tree &source, const schema_record_field &target) {
            if (const auto primary = find_field(source, target.id)) { return {primary}; }
            field_match match{};
            for (auto id: target.accepted_ids()) {
                if (const auto alias = find_field(source, id)) {
                    if (match.index) { return {.ambiguous = true}; }
                    match.index = alias;
                }
            }
            return match;
        }

        template<std::size_t Count>
        consteval auto match_fields(const schema_tree &source, const schema_tree &target) {
            std::array<field_match, Count> matches{};
            const auto fields = target.child_indices(target.value_index());
            for (std::size_t i = 0; i < Count; ++i) {
                matches[i] = match_field(source, target[fields[i]].record_field());
            }
            return matches;
        }

        consteval bool has_nondefaulted_copy_or_move(std::meta::info type) {
            for (auto member: std::meta::members_of(type, std::meta::access_context::unchecked())) {
                if (std::meta::is_special_member_function(member) && std::meta::is_user_declared(member)
                    && !std::meta::is_defaulted(member)
                    && (std::meta::is_copy_constructor(member) || std::meta::is_move_constructor(member)
                        || std::meta::is_copy_assignment(member) || std::meta::is_move_assignment(member))) {
                    return true;
                }
            }
            return false;
        }

        template<typename To, typename From>
        concept non_narrowing_numeric_assignment =
            !std::is_arithmetic_v<To> || !std::is_arithmetic_v<From>
            || requires(const From &value) { To{value}; };

        template<typename T>
        concept memberwise_record = reflected_record<T>
            && (!has_nondefaulted_copy_or_move(std::meta::remove_cvref(^^T)))
            && xasrl::detail::schema_for<T>.value_node().kind() == schema_kind::record;

        template<typename To, typename From>
        constexpr void assign_fields(To &dst, From &&src) {
            constexpr auto &target = xasrl::detail::schema_for<To>.tree;
            constexpr auto &source = xasrl::detail::schema_for<std::remove_cvref_t<From>>.tree;
            constexpr auto fields = target.child_indices(target.value_index());
            constexpr auto matches = match_fields<fields.size()>(source, target);
            if constexpr (!std::is_lvalue_reference_v<From>) {
                static_assert([](const auto &plan, auto source_size) consteval {
                    std::array<bool, decltype(source_size)::value> used{};
                    for (auto match: plan) {
                        if (match.index) {
                            if (used[*match.index]) { return false; }
                            used[*match.index] = true;
                        }
                    }
                    return true;
                }(matches, std::integral_constant<std::size_t, source.size()>{}),
                    "a source field cannot be matched by multiple targets in schema move assignment");
            }
            template for (constexpr auto ordinal: std::views::iota(std::size_t{}, fields.size())) {
                constexpr auto field = target[fields[ordinal]].record_field().field;
                constexpr auto match = matches[ordinal];
                static_assert(!match.ambiguous, "multiple source IDs match the target field's accept_id aliases");
                if constexpr (match.index) {
                    constexpr auto input = source[*match.index].field();
                    using to_type = [:field.memory_type():];
                    using from_type = [:input.memory_type():];
                    if constexpr (!non_narrowing_numeric_assignment<to_type, from_type>) {
                        static_assert(false, "schema assignment would narrow a numeric field");
                    } else if constexpr (requires {
                        dst.[:field.entity:] = std::forward_like<From>(src.[:input.entity:]);
                    }) {
                        dst.[:field.entity:] = std::forward_like<From>(src.[:input.entity:]);
                    } else {
                        static_assert(false, "matched schema fields are not assignable");
                    }
                }
            }
        }
    }

    /** Preserves the source's value category; exceptions propagate without state guarantees. */
    template<detail::memberwise_record To, detail::memberwise_record From>
        requires (!std::is_volatile_v<std::remove_reference_t<From>>)
            && (!std::is_const_v<To> && !std::is_volatile_v<To>)
    constexpr void transform_assign(To &dst, From &&src) {
        detail::assign_fields(dst, std::forward<From>(src));
    }
}
