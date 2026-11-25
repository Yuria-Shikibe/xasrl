#pragma once

#include <meta>

#include <array>
#include <concepts>
#include <cstddef>
#include <expected>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

#include "detail/codec.hpp"
#include "schema.hpp"
#include "transaction.hpp"
#include "storage/byte_buffer.hpp"
#include "storage/serialization_workspace.hpp"

namespace srl {
    // A prepared operation keeps conversion results and dynamic layout metadata alive
    // across measurement and encoding. This is a borrow, not a snapshot: the source
    // (including its referenced data, shape and order) must remain alive and unchanged.
    // The workspace must outlive the plan and cannot be reset until every plan using
    // it is destroyed. A moved-from plan may only be destroyed.
    template<serializable T>
    class prepared_serialization {
        using plan_type = typename detail::root_codec<T>::encode_plan;

        const T *value_;
        plan_type plan_;

        prepared_serialization(serialization_workspace &workspace,
                               const T &value,
                               const serialization_context &context)
            : value_(std::addressof(value)), plan_(workspace.resource()) {}

    public:
        prepared_serialization(prepared_serialization &&) = default;
        prepared_serialization &operator=(prepared_serialization &&) = delete;
        prepared_serialization(const prepared_serialization &) = delete;
        prepared_serialization &operator=(const prepared_serialization &) = delete;

        [[nodiscard]] static std::expected<prepared_serialization, error> prepare(
            serialization_workspace &workspace,
            const T &value,
            const serialization_context &context) {
            prepared_serialization result{workspace, value, context};
            if constexpr (auto status = result.plan_.prepare(value, context, workspace.resource());
                          std::same_as<decltype(status), error>) {
                if (status) { return std::unexpected{status}; }
            }
            return result;
        }

        static std::expected<prepared_serialization, error> prepare(
            serialization_workspace &, const T &&, const serialization_context &) = delete;

        [[nodiscard]] szt size() const noexcept { return plan_.size; }

        template<std::size_t Extent>
            requires (Extent == std::dynamic_extent
                      || plan_type::static_extent == std::dynamic_extent
                      || Extent == plan_type::static_extent)
        [[nodiscard]] detail::combined_status<
            detail::status_type<Extent != std::dynamic_extent && plan_type::static_extent != std::dynamic_extent>,
            typename plan_type::encode_result> encode(std::span<std::byte, Extent> destination) const {
            if constexpr (Extent == std::dynamic_extent || plan_type::static_extent == std::dynamic_extent) {
                if (destination.size() != plan_.size) {
                    return detail::fail(error_code::invalid_entry_size);
                }
            }
            return plan_.encode(detail::payload_span<plan_type::static_extent>(destination, 0, plan_.size), *value_);
        }
    };

    template<serializable T>
        requires (!detail::root_encode_extent<T>.is_dynamic())
    [[nodiscard]] consteval szt serialized_size() noexcept {
        return detail::root_encode_extent<T>.value;
    }

    template<serializable T>
        requires (!detail::root_encode_extent<T>.is_dynamic())
    [[nodiscard]] constexpr szt serialized_size(
        const T &, const serialization_context &) noexcept {
        return serialized_size<T>();
    }

    //TODO should get size returns error?
    template<serializable T>
        requires (detail::root_encode_extent<T>.is_dynamic())
    [[nodiscard]] std::expected<szt, error> serialized_size(
        const T &value, const serialization_context &context) {
        using workspace_type = std::conditional_t<detail::root_codec<T>::uses_workspace,
            serialization_workspace, detail::no_serialization_workspace>;
        workspace_type workspace;
        typename detail::root_codec<T>::encode_plan plan{workspace.resource()};
        if constexpr (auto status = plan.prepare(value, context, workspace.resource()); std::same_as<decltype(status), error>) {
            if (status) { return std::unexpected{status}; }
        }
        return plan.size;
    }

    template<serializable T, std::size_t Extent>
        requires (Extent == std::dynamic_extent
                  || detail::root_encode_extent<T>.is_dynamic()
                  || Extent == detail::root_encode_extent<T>.value)
    [[nodiscard]] detail::combined_status<
        detail::status_type<Extent != std::dynamic_extent
            && !detail::root_encode_extent<T>.is_dynamic()>,
        typename detail::root_codec<T>::encode_plan::prepare_result,
        typename detail::root_codec<T>::encode_plan::encode_result> serialize(
        const T &value,
        std::span<std::byte, Extent> destination,
        const serialization_context &context) {
        using workspace_type = std::conditional_t<detail::root_codec<T>::uses_workspace,
            serialization_workspace, detail::no_serialization_workspace>;
        workspace_type workspace;
        typename detail::root_codec<T>::encode_plan plan{workspace.resource()};
        if constexpr (auto status = plan.prepare(value, context, workspace.resource()); std::same_as<decltype(status), error>) {
            if (status) { return status; }
        }
        if constexpr (Extent == std::dynamic_extent || plan.static_extent == std::dynamic_extent) {
            if (destination.size() != plan.size) {
                return detail::fail(error_code::invalid_entry_size);
            }
        }
        return plan.encode(detail::payload_span<plan.static_extent>(destination, 0, plan.size), value);
    }

    template<serializable T>
    [[nodiscard]] error serialize(
        const T &value, byte_buffer &destination, const serialization_context &context) {
        using workspace_type = std::conditional_t<detail::root_codec<T>::uses_workspace,
            serialization_workspace, detail::no_serialization_workspace>;
        workspace_type workspace;
        typename detail::root_codec<T>::encode_plan plan{workspace.resource()};
        if constexpr (auto status = plan.prepare(value, context, workspace.resource()); std::same_as<decltype(status), error>) {
            if (status) { return status; }
        }
        if (plan.size > destination.max_size()) {
            return detail::fail(error_code::entry_too_large);
        }
        destination.resize(static_cast<std::size_t>(plan.size));
        return plan.encode(detail::payload_span<plan.static_extent>(destination.span(), 0, plan.size), value);
    }

    template<serializable T, typename Byte, std::size_t Extent>
        requires (std::meta::is_same_type(^^Byte, ^^std::byte) || std::meta::is_same_type(^^Byte, ^^const std::byte))
                 && (!std::meta::is_const_type(^^T) && !std::meta::is_volatile_type(^^T))
                 && transactional<codec_value_type<T>>
    [[nodiscard]] error deserialize(
        std::span<Byte, Extent> bytes,
        T &out,
        const serialization_context &context) {
        if (bytes.size() > context.config.limits.max_input_bytes) {
            return detail::fail(error_code::resource_limit_exceeded);
        }
        auto entry = detail::parse_entry(bytes);
        if (!entry) { return entry.error(); }
        using root = detail::root_codec<T>;
        constexpr auto &descriptor = root::descriptor;
        if (auto status = detail::validate_entry_type(*entry, descriptor.wire_kind(), descriptor.wire_type()); status) {
            return status;
        }
        auto temporary = [&] {
            if constexpr (root::descriptor.kind() == schema_kind::container
                && requires { transaction_traits<codec_value_type<T>>::stage(out); }) {
                return transaction_traits<codec_value_type<T>>::stage(out);
            } else {
                return transaction_traits<codec_value_type<T>>::clone(out);
            }
        }();
        if (auto decoded = root::decode_entry(*entry, temporary, context); decoded) {
            return decoded;
        }
        transaction_traits<codec_value_type<T>>::commit(out, temporary);
        return {};
    }

    template<typename T, std::size_t Size, typename Byte, std::size_t Extent>
        requires serializable<T[Size]>
                 && (std::meta::is_same_type(^^Byte, ^^std::byte) || std::meta::is_same_type(^^Byte, ^^const std::byte))
                 && (!std::meta::is_const_type(^^T) && !std::meta::is_volatile_type(^^T))
                 && transactional<T>
    [[nodiscard]] error deserialize(
        std::span<Byte, Extent> bytes,
        T (&out)[Size],
        const serialization_context &context) {
        if (bytes.size() > context.config.limits.max_input_bytes) {
            return detail::fail(error_code::resource_limit_exceeded);
        }
        auto entry = detail::parse_entry(bytes);
        if (!entry) { return entry.error(); }
        auto temporary = [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return std::array<T, Size>{transaction_traits<T>::clone(out[Indices])...};
        }(std::make_index_sequence<Size>{});
        if (auto decoded = detail::root_codec<decltype(temporary)>::decode_entry(*entry, temporary, context); decoded) {
            return decoded;
        }
        for (std::size_t index = 0; index != Size; ++index) {
            transaction_traits<T>::commit(out[index], temporary[index]);
        }
        return {};
    }
}
