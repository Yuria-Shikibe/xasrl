#pragma once

#include <algorithm>
#include <cstddef>
#include <meta>
#include <span>
#include <vector>

#include "../annotations.hpp"

namespace xasrl {
    consteval std::meta::info recursively_dealias(std::meta::info info) {
        while (std::meta::is_type_alias(info)) {
            info = std::meta::dealias(info);
        }
        return info;
    }

}

namespace xasrl::detail {
    consteval std::size_t annotation_count(std::meta::info entity, std::meta::info annotation) {
        entity = recursively_dealias(entity);
        return std::meta::annotations_of_with_type(entity, annotation).size();
    }

    consteval std::vector<std::meta::info> reflected_entities(std::meta::info type) {
        auto result = std::meta::bases_of(type, std::meta::access_context::unchecked());
        result.append_range(std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked()));
        return result;
    }

    consteval bool schema_has_fields(std::meta::info type) {
        type = recursively_dealias(type);

        //TODO is union here should be supported ?
        if (!std::meta::is_class_type(type)) {
            return false;
        }
        if (annotation_count(type, ^^tag::auto_seq) != 0) {
            return true;
        }

        return std::ranges::any_of(reflected_entities(type), [](std::meta::info i) {
            return annotation_count(i, ^^tag::field) != 0;
        });
    }

    consteval bool schema_contains_pointer(std::meta::info type) {
        if (std::meta::is_reference_type(type) || std::meta::is_pointer_type(type) || std::meta::is_member_pointer_type(type)) {
            return true;
        }
        type = std::meta::remove_cv(type);
        if (std::meta::is_array_type(type)) {
            return schema_contains_pointer(std::meta::remove_extent(type));
        }
        if (std::meta::is_class_type(type) || std::meta::is_union_type(type)) {
            return std::ranges::any_of(reflected_entities(type), [](std::meta::info i) {
                return schema_contains_pointer(std::meta::type_of(i));
            });
        }
        return false;
    }


    struct entity_facts {
        std::meta::info declared;
        bool base;
        bool unsupported;
        std::size_t fields;
        std::size_t leaves;
        std::size_t containers;
        std::size_t conversions;
        std::size_t transient;
        std::size_t bypass;
        std::size_t streams;
        std::size_t fallbacks;
        std::size_t validation_bypasses;

        consteval explicit entity_facts(std::meta::info entity)
            : declared(std::meta::type_of(entity)), base(std::meta::is_base(entity)),
              unsupported(std::meta::is_bit_field(entity)
                  || (base && std::meta::is_virtual(entity))),
              fields(annotation_count(entity, ^^tag::field)),
              leaves(annotation_count(entity, ^^tag::leaf)),
              containers(annotation_count(entity, ^^tag::container)),
              conversions(annotation_count(entity, ^^conversion_tag)),
              transient(annotation_count(entity, ^^tag::transient)),
              bypass(annotation_count(entity, ^^tag::bypass_pointer_check)),
              streams(annotation_count(entity, ^^tag::auto_stream)),
              fallbacks(fallback_annotation_count(entity)),
              validation_bypasses(annotation_count(entity, ^^tag::bypass_validation)) {}
    };

    // Shared by automatic leaf eligibility and schema validation, including
    // explicitly specialized leaf codecs. Returns the offending entity.
    consteval std::meta::info leaf_metadata_issue(std::meta::info type) {
        if (!std::meta::is_class_type(type)
            || annotation_count(type, ^^tag::auto_seq) != 0
            || annotation_count(type, ^^tag::auto_stream) != 0
            || annotation_count(type, ^^tag::bypass_pointer_check) > 1
            || annotation_count(type, ^^tag::bypass_validation) > 1) {
            return type;
        }
        for (auto entity: reflected_entities(type)) {
            const entity_facts facts{entity};
            if (annotation_count(entity, ^^tag::inline_mapping) != 0 || facts.streams != 0 || facts.transient > 1 || facts.bypass > 1 || facts.validation_bypasses > 1
                || facts.fields != 0 || facts.leaves != 0 || facts.containers != 0
                || facts.fallbacks != 0 || facts.conversions != 0
                || annotation_count(entity, ^^tag::accept_id) != 0) {
                return entity;
            }
            if (facts.transient == 1) {
                if (facts.base || facts.bypass != 0 || facts.validation_bypasses != 0) { return entity; }
                continue;
            }
            if (facts.unsupported || std::meta::is_reference_type(facts.declared)
                || (schema_contains_pointer(facts.declared)
                    && annotation_count(type, ^^tag::bypass_pointer_check) == 0 && facts.bypass == 0)) {
                return entity;
            }
        }
        return {};
    }

    struct byte_range {
        std::size_t offset{};
        std::size_t size{};
    };

    consteval bool has_transient_members(std::meta::info type) {
        for (auto entity: reflected_entities(type)) {
            if (!std::meta::is_base(entity) && annotation_count(entity, ^^tag::transient) != 0) {
                return true;
            }
        }
        return false;
    }

    consteval bool transient_layout_supports_segmented_copy(std::meta::info type) {
        const auto entities = reflected_entities(type);
        std::vector<byte_range> transient_ranges;
        for (auto entity: entities) {
            if (annotation_count(entity, ^^tag::transient) == 1) {
                if (std::meta::is_base(entity) || std::meta::is_bit_field(entity)) {
                    return false;
                }
                const auto offset = std::meta::offset_of(entity);
                if (offset.bits != 0) { return false; }
                transient_ranges.push_back({static_cast<std::size_t>(offset.bytes), std::meta::size_of(entity)});
            }
        }
        for (auto entity: entities) {
            if (annotation_count(entity, ^^tag::transient) == 0) {
                const auto offset = std::meta::offset_of(entity);
                if (offset.bits != 0) { return false; }
                const auto begin = static_cast<std::size_t>(offset.bytes);
                const auto end = begin + std::meta::size_of(entity);
                for (const auto transient: transient_ranges) {
                    if (begin < transient.offset + transient.size && transient.offset < end) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    consteval auto leaf_copy_ranges(std::meta::info type) {
        std::vector<byte_range> excluded;
        for (auto entity: reflected_entities(type)) {
            if (annotation_count(entity, ^^tag::transient) == 1) {
                excluded.push_back({static_cast<std::size_t>(std::meta::offset_of(entity).bytes),
                                    std::meta::size_of(entity)});
            }
        }
        std::ranges::sort(excluded, {}, &byte_range::offset);
        std::vector<byte_range> result;
        std::size_t begin{};
        for (const auto range: excluded) {
            if (begin < range.offset) { result.push_back({begin, range.offset - begin}); }
            begin = std::max(begin, range.offset + range.size);
        }
        if (begin < std::meta::size_of(type)) {
            result.push_back({begin, std::meta::size_of(type) - begin});
        }
        return std::define_static_array(result);
    }

    consteval std::size_t byte_ranges_size(std::span<const byte_range> ranges) {
        std::size_t result{};
        for (const auto range: ranges) { result += range.size; }
        return result;
    }

    // The caller establishes exactly one leaf annotation before reading its options.
    consteval tag::leaf leaf_options(std::meta::info type) {
        return std::meta::extract<tag::leaf>(
            std::meta::annotations_of_with_type(type, ^^tag::leaf).front());
    }

    consteval bool leaf_allows_object_representation(std::meta::info type) {
        return leaf_options(type).allow_non_unique_presentation
            || std::meta::has_unique_object_representations(type);
    }

    consteval bool leaf_uses_direct_object_representation_impl(std::meta::info type) {
        return !has_transient_members(type)
            && leaf_allows_object_representation(type);
    }

    consteval bool leaf_uses_segmented_object_representation_impl(std::meta::info type) {
        return has_transient_members(type)
            && leaf_allows_object_representation(type)
            && std::meta::is_trivially_copyable_type(type)
            && transient_layout_supports_segmented_copy(type);
    }

    consteval bool auto_leaf_supported(std::meta::info type) {
        return std::meta::is_class_type(type)
            && annotation_count(type, ^^tag::leaf) == 1
            && annotation_count(type, ^^tag::auto_seq) == 0
            && annotation_count(type, ^^tag::auto_stream) == 0
            && annotation_count(type, ^^tag::bypass_pointer_check) <= 1
            && (leaf_uses_direct_object_representation_impl(type)
                || leaf_uses_segmented_object_representation_impl(type))
            && std::meta::is_trivially_copyable_type(type)
            && leaf_metadata_issue(type) == std::meta::info{};
    }

}
