#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <meta>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "../schema.hpp"
#include "../detail/reflection_facts.hpp"

namespace xasrl {
    namespace detail {
        consteval std::meta::info find_field(const schema_tree &tree, id_type id) {
            for (auto index: tree.child_indices(tree.value_index())) {
                const auto &candidate = tree[index].record_field();
                if (candidate.id == id) { return candidate.field.entity; }
                if (candidate.id > id) { break; }
            }
            return {};
        }

        struct field_match {
            std::meta::info entity{};
            bool ambiguous{};

            consteval void add(std::meta::info candidate) {
                if (entity == std::meta::info{}) { entity = candidate; }
                else if (entity != candidate) { ambiguous = true; }
            }
        };

        struct transform_field {
            std::meta::info entity;
            id_type id{};
            bool has_id{};
        };

        consteval bool has_transform_alias(std::meta::info entity) {
            return annotation_count(entity, ^^tag::accept_member) != 0 || annotation_count(entity, ^^tag::accept_id) != 0;
        }

        consteval bool has_transform_targets(std::meta::info type) {
            if (!std::meta::is_class_type(type) || !std::meta::is_complete_type(type)) { return false; }
            for (auto entity: reflected_entities(type)) {
                if (has_transform_alias(entity)) { return true; }
            }
            return false;
        }

        consteval field_match match_field(const schema_tree &source, std::span<const std::meta::info> entities,
                                          transform_field target) {
            field_match match{};
            for (auto annotation: std::meta::annotations_of_with_type(target.entity, ^^tag::accept_member)) {
                const auto entity = std::meta::extract<tag::accept_member>(annotation).where;
                if (std::ranges::find(entities, entity) != entities.end()) { match.add(entity); }
            }
            if (match.entity != std::meta::info{}) { return match; }
            if (target.has_id) {
                if (const auto primary = find_field(source, target.id); primary != std::meta::info{}) { return {primary}; }
            }
            for (auto annotation: std::meta::annotations_of_with_type(target.entity, ^^tag::accept_id)) {
                const auto id = std::meta::extract<tag::accept_id>(annotation).id;
                if (const auto alias = find_field(source, id); alias != std::meta::info{}) {
                    match.add(alias);
                }
            }
            return match;
        }

        template<std::size_t Count>
        consteval auto match_fields(const schema_tree &source, std::meta::info source_type,
                                    std::span<const transform_field> fields) {
            std::array<field_match, Count> matches{};
            const auto entities = xasrl::detail::reflected_entities(source_type);
            for (std::size_t i = 0; i < Count; ++i) {
                matches[i] = match_field(source, entities, fields[i]);
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
        concept memberwise_record =
#ifdef __RESHARPER__
            true;
#else
            reflected_record<T> && schema{^^T}.value_node().kind() == schema_kind::record;
#endif

        template<typename From, typename To>
        concept record_transformable = memberwise_record<From>
            && (memberwise_record<To> || has_transform_targets(^^To))
            && (!(has_nondefaulted_copy_or_move(^^From) || has_nondefaulted_copy_or_move(^^To)) || annotation_count(^^To, ^^tag::extractor) != 0);

        template<typename To>
        consteval auto transform_fields() {
            auto transform_fields_impl = []() {
                std::vector<transform_field> fields;
                if constexpr (memberwise_record<To>) {
                    constexpr auto target = schema{^^To}.tree;
                    for (auto index: target.child_indices(target.value_index())) {
                        const auto &field = target[index].record_field();
                        fields.push_back({field.field.entity, field.id, true});
                    }
                }
                const auto primary_count = fields.size();
                for (auto entity: reflected_entities(^^To)) {
                    if (has_transform_alias(entity)
                        && std::ranges::find(fields.begin(), fields.begin() + primary_count, entity, &transform_field::entity)
                        == fields.begin() + primary_count) {
                        fields.push_back({entity});
                    }
                }

                return fields;
            };

            auto v = transform_fields_impl();
            std::array<transform_field, transform_fields_impl().size()> a;
            std::ranges::copy(v, a.begin());
            return a;
        }

        template<typename To, typename From>
        constexpr void assign_fields(To &dst, From &&src) {
            static constexpr auto source = schema{std::meta::remove_cvref(^^From)}.tree;
            static constexpr auto fields = transform_fields<To>();
            static constexpr auto matches = detail::match_fields<fields.size()>(source, std::meta::remove_cvref(^^From), fields);
            if constexpr (!std::is_lvalue_reference_v<From>) {
                static_assert([](const auto &plan) consteval {
                    for (auto it = plan.begin(); it != plan.end(); ++it) {
                        if (it->entity != std::meta::info{}
                            && std::ranges::find(plan.begin(), it, it->entity, &field_match::entity) != it) {
                            return false;
                        }
                    }
                    return true;
                }(matches),
                    "a source field cannot be matched by multiple targets in schema move assignment");
            }
            template for (constexpr auto ordinal: std::views::iota(std::size_t{}, fields.size())) {
                static constexpr std::meta::info field = fields[ordinal].entity;
                static constexpr field_match match = matches[ordinal];
                static_assert(!match.ambiguous, "multiple source fields match the target field's accept_member or accept_id aliases");
                if constexpr (match.entity != std::meta::info{}) {
                    using to_type = [:std::meta::remove_cvref(std::meta::type_of(field)):];
                    using from_type = [:std::meta::remove_cvref(std::meta::type_of(match.entity)):];
                    if constexpr (!non_narrowing_numeric_assignment<to_type, from_type>) {
                        static_assert(false, "schema assignment would narrow a numeric field");
                    } else if constexpr (requires {
                        dst.[:field:] = std::forward_like<From>(src.[:match.entity:]);
                    }) {
                        dst.[:field:] = std::forward_like<From>(src.[:match.entity:]);
                    } else {
                        static_assert(false, "matched schema fields are not assignable");
                    }
                }
            }
        }
    }

    /** Preserves the source's value category; exceptions propagate without state guarantees. */
    template<typename  To, typename  From>
        requires (!std::is_volatile_v<std::remove_reference_t<From>>)
            && (!std::is_const_v<To> && !std::is_volatile_v<To>) && detail::record_transformable<std::remove_cvref_t<From>, std::remove_cvref_t<To>>
    constexpr void transform_assign(To &dst, From &&src) {
        detail::assign_fields(dst, std::forward<From>(src));
    }
}
