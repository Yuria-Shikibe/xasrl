#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <meta>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include "encode.hpp"

namespace srl {
    namespace detail {
        template<typename T>
        [[nodiscard]] std::expected<void, error> decode_value(
            std::span<const std::byte> source,
            T &out,
            const serialization_context &context,
            id_type id = 0,
            std::uint32_t offset = 0,
            std::optional<duplicate_policy> local_duplicates = std::nullopt);

        template<reflected_record T>
        [[nodiscard]] std::expected<void, error> decode_record(
            const entry_view &entry,
            T &out,
            const serialization_context &context) {
            static_assert(schema_valid<T>(), "invalid reflected serialization schema");
            if (entry.head.kind != entry_kind::record
                || entry.head.mapping != mapping_kind::field
                || entry.head.mapping_group_size != 1) {
                return fail(error_code::entry_kind_mismatch, 0, 9);
            }
            std::expected<void, error> status{};
            detail::for_each_serialized_entity<T>(
                [&]<std::meta::info Entity, reflected_entity_kind Kind>(tag::field annotation) {
                    if (!status) {
                        return;
                    }
                    auto field = entry.find(annotation.id);
                    if (!field) {
                        status = std::unexpected{field.error()};
                        return;
                    }
                    static constexpr auto container_annotations = std::define_static_array(
                        std::meta::annotations_of_with_type(Entity, ^^tag::container));
                    std::optional<duplicate_policy> local_duplicates{};
                    if constexpr (container_annotations.size() == 1) {
                        static constexpr auto container_annotation =
                            std::meta::extract<tag::container>(container_annotations.front());
                        local_duplicates = container_annotation.duplicates;
                    }
                    if constexpr (conversion_count<Entity>() == 1) {
                        using memory_type = reflected_entity_value_t<Entity>;
                        using storage_type = field_storage_t<Entity>;
                        constexpr auto conversion = conversion_options<Entity>();
                        auto &mem = entity_reference<Entity, Kind>(out);
                        auto decode_storage = [&](storage_type &storage) {
                            return detail::decode_value(field->bytes, storage, context,
                                                        annotation.id, field->offset,
                                                        local_duplicates);
                        };
                        if constexpr (conversion.handler_id == nullptr) {
                            storage_type storage{};
                            status = decode_storage(storage);
                            if (status) {
                                mem = static_cast<memory_type>(std::move(storage));
                            }
                        } else {
                            auto &handler = context.template get_handler<memory_type, storage_type>(
                                conversion.handler_id);
                            storage_type storage{};
                            status = decode_storage(storage);
                            if (status) {
                                status = handler.srl2mem(std::addressof(mem), std::addressof(storage));
                            }
                        }
                        if (!status) {
                            status = std::unexpected{locate_error(
                                status.error(), annotation.id, field->offset)};
                        }
                    } else if constexpr (entity_uses_forced_object_representation<T, Entity>()) {
                        status = detail::decode_forced_atomic_field<T, Entity>(
                            field->bytes,
                            detail::entity_reference<Entity, Kind>(out));
                        if (!status) {
                            auto failure = status.error();
                            if (failure.id == 0) {
                                failure.id = annotation.id;
                            }
                            if (failure.offset == 0) {
                                failure.offset = field->offset;
                            }
                            status = std::unexpected{failure};
                        }
                    } else {
                        status = detail::decode_value(
                            field->bytes,
                            detail::entity_reference<Entity, Kind>(out),
                            context,
                            annotation.id,
                            field->offset,
                            local_duplicates);
                    }
                });
            return status;
        }

        template<container_serializable T>
        [[nodiscard]] std::expected<void, error> decode_container(
            const entry_view &entry,
            T &out,
            const serialization_context &context,
            std::optional<duplicate_policy> local_duplicates) {
            using traits = container_traits<T>;
            constexpr auto group_size = traits::mapping_group_size;
            if (entry.head.kind != entry_kind::container
                || entry.head.mapping != mapping_kind::element
                || entry.head.mapping_group_size != group_size) {
                return fail(error_code::entry_kind_mismatch, 0, 9);
            }

            const auto element_count = entry.head.mapping_count / group_size;
            if constexpr (detail::fixed_container<T>) {
                if (element_count != traits::fixed_size) {
                    return fail(error_code::fixed_container_size_mismatch, 0, 4);
                }
            } else {
                detail::clear_container(out);
                detail::reserve_container(out, element_count);
            }

            const auto duplicates = local_duplicates.value_or(context.config.duplicates);
            auto decode_slot = [&](std::size_t index, auto &value) -> std::expected<void, error> {
                auto slot = entry.find(static_cast<id_type>(index));
                if (!slot) {
                    return std::unexpected{slot.error()};
                }
                return detail::decode_value(
                    slot->bytes,
                    value,
                    context,
                    static_cast<id_type>(index),
                    slot->offset);
            };

            if constexpr (group_size == 1) {
                if constexpr (detail::fixed_container<T>) {
                    auto iterator = std::ranges::begin(detail::container_range(out));
                    for (std::size_t index = 0; index != element_count; ++index, ++iterator) {
                        if (auto decoded = decode_slot(index, *iterator); !decoded) {
                            return decoded;
                        }
                    }
                } else if constexpr (detail::insert_after_container<T>) {
                    auto cursor = out.before_begin();
                    for (std::size_t index = 0; index != element_count; ++index) {
                        typename traits::element_type element{};
                        if (auto decoded = decode_slot(index, element); !decoded) {
                            return decoded;
                        }
                        cursor = out.insert_after(cursor, std::move(element));
                    }
                } else {
                    for (std::size_t index = 0; index != element_count; ++index) {
                        typename traits::element_type element{};
                        if (auto decoded = decode_slot(index, element); !decoded) {
                            return decoded;
                        }
                        const auto inserted = detail::insert_container_value(out, std::move(element));
                        if (!inserted
                            && duplicates == duplicate_policy::reject) {
                            return fail(
                                error_code::duplicate_container_element,
                                static_cast<id_type>(index));
                        }
                    }
                }
            } else {
                for (std::size_t index = 0; index != element_count; ++index) {
                    typename traits::key_type key{};
                    typename traits::mapped_type mapped{};
                    if (auto decoded = decode_slot(index * 2, key); !decoded) {
                        return decoded;
                    }
                    if (auto decoded = decode_slot(index * 2 + 1, mapped); !decoded) {
                        return decoded;
                    }
                    typename traits::element_type element{
                        std::move(key),
                        std::move(mapped),
                    };
                    const auto inserted = detail::insert_container_value(out, std::move(element));
                    if (!inserted
                        && duplicates == duplicate_policy::reject) {
                        return fail(
                            error_code::duplicate_container_element,
                            static_cast<id_type>(index * 2));
                    }
                }
            }
            return {};
        }

        template<typename T>
        [[nodiscard]] std::expected<void, error> decode_value(
            std::span<const std::byte> source,
            T &out,
            const serialization_context &context,
            id_type id,
            std::uint32_t offset,
            std::optional<duplicate_policy> local_duplicates) {
            if constexpr (atomic_serializable<T>) {
                using value_type = codec_value_type<T>;
                constexpr auto fixed_size = atomic_codec<value_type>::fixed_size;
                if (source.size() != fixed_size) {
                    return fail(error_code::invalid_payload_shape, id, offset);
                }
                auto decoded = atomic_codec<value_type>::decode(
                    source.template first<fixed_size>(), out);
                if (!decoded) {
                    auto error = decoded.error();
                    if (error.id == 0) {
                        error.id = id;
                    }
                    if (error.offset == 0) {
                        error.offset = offset;
                    }
                    return std::unexpected{error};
                }
                return {};
            } else if constexpr (container_serializable<T>) {
                auto entry = parse_entry(source);
                if (!entry) {
                    return std::unexpected{entry.error()};
                }
                return detail::decode_container(
                    *entry,
                    out,
                    context,
                    local_duplicates);
            } else {
                auto entry = parse_entry(source);
                if (!entry) {
                    return std::unexpected{entry.error()};
                }
                return detail::decode_record(*entry, out, context);
            }
        }
    }
}
