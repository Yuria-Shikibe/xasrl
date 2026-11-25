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
#include "storage/encoding_recorder.hpp"

namespace xasrl {
    namespace detail {
        template<serializable T, schema_storage Mode, bool Recorded = root_codec<T>::uses_recorder>
        class encoding_operation {
            using plan_type = typename root_codec<T>::template encoding_plan<Mode>;
            //TODO use adapted attribute
            [[no_unique_address]] plan_type plan_;

        public:
            encoding_operation() = default;
            explicit encoding_operation(encoding_recorder &) {}

            plan_type &plan() noexcept { return plan_; }
            const plan_type &plan() const noexcept { return plan_; }
            static encoding_session *writer() noexcept { return nullptr; }
            static encoding_cursor cursor() noexcept { return {}; }
        };

        template<serializable T, schema_storage Mode>
        class encoding_operation<T, Mode, true> {
            using plan_type = typename root_codec<T>::template encoding_plan<Mode>;
            encoding_session session_;
            plan_type &plan_;

        public:
            explicit encoding_operation(encoding_recorder &recorder)
                : session_(recorder), plan_(session_.append<plan_type>()) {}

            plan_type &plan() noexcept { return plan_; }
            const plan_type &plan() const noexcept { return plan_; }
            encoding_session *writer() noexcept { return &session_; }
            encoding_cursor cursor() const noexcept {
                auto reader = session_.cursor();
                (void)reader.read<plan_type>(); // The first record holds the root plan itself.
                return reader;
            }
        };

        template<typename T, std::size_t Extent, schema_storage Mode>
        concept encoding_destination = serializable<T> && (Extent == std::dynamic_extent
            || root_encode_extent<T, Mode>.is_dynamic() || Extent == root_encode_extent<T, Mode>.value);

        template<serializable T, std::size_t Extent, schema_storage Mode>
        using prepared_encode_result = combined_status<
            status_type<Extent != std::dynamic_extent && !root_encode_extent<T, Mode>.is_dynamic()>,
            typename root_codec<T>::encode_plan::encode_result>;

        template<serializable T, std::size_t Extent, schema_storage Mode>
        using serialize_result = combined_status<
            prepared_encode_result<T, Extent, Mode>, typename root_codec<T>::encode_plan::prepare_result>;

        template<serializable T, schema_storage Mode>
        std::expected<szt, error> measure_with(
            encoding_operation<T, Mode> &operation, const T &value, const serialization_context &context) {
            auto &plan = operation.plan();
            if constexpr (auto status = plan.prepare(value, context, operation.writer()); std::same_as<decltype(status), error>) {
                if (status) { return std::unexpected{status}; }
            }
            return plan.size;
        }

        template<serializable T, std::size_t Extent, schema_storage Mode>
        prepared_encode_result<T, Extent, Mode> encode_to_span(
            const encoding_operation<T, Mode> &operation, const T &value, std::span<std::byte, Extent> destination) {
            const auto &plan = operation.plan();
            if constexpr (Extent == std::dynamic_extent || plan.static_extent == std::dynamic_extent) {
                if (destination.size() != plan.size) {
                    return fail(error_code::invalid_entry_size);
                }
            }
            auto records = operation.cursor();
            return plan.encode(payload_span<plan.static_extent>(destination, 0, plan.size), value, records);
        }

        template<serializable T, typename Destination, schema_storage Mode>
        auto serialize_with(encoding_operation<T, Mode> &operation, const T &value, Destination &destination,
                            const serialization_context &context) {
            auto &plan = operation.plan();
            using result_type = decltype([] {
                if constexpr (std::same_as<Destination, byte_buffer>) {
                    return error{};
                } else {
                    return serialize_result<T, Destination::extent, Mode>{};
                }
            }());
            if constexpr (auto status = plan.prepare(value, context, operation.writer()); std::same_as<decltype(status), error>) {
                if (status) { return result_type{status}; }
            }
            if constexpr (std::same_as<Destination, byte_buffer>) {
                if (plan.size > destination.max_size()) {
                    return fail(error_code::entry_too_large);
                }
                destination.resize(static_cast<std::size_t>(plan.size));
                auto records = operation.cursor();
                return static_cast<error>(plan.encode(
                    payload_span<plan.static_extent>(destination.span(), 0, plan.size), value, records));
            } else {
                return result_type{encode_to_span(operation, value, destination)};
            }
        }
    }

    // The source (including its referenced data, shape and order) is borrowed and
    // must remain unchanged. A recorder, if used, must outlive the prepared handle.
    // A moved-from handle may only be destroyed.
    template<serializable T, schema_storage Mode = schema_storage::embedded>
    class prepared_serialization {
        const T *value_;
        [[no_unique_address]] detail::encoding_operation<T, Mode> operation_;

        template<typename... Args>
        explicit prepared_serialization(const T &value, Args &...args)
            : value_(std::addressof(value)), operation_(args...) {}

        template<typename... Args>
        static std::expected<prepared_serialization, error> prepare_with(
            const T &value, const serialization_context &context, Args &...args) {
            prepared_serialization result{value, args...};
            if constexpr (auto status = result.operation_.plan().prepare(value, context, result.operation_.writer());
                          std::same_as<decltype(status), error>) {
                if (status) { return std::unexpected{status}; }
            }
            return result;
        }

    public:
        prepared_serialization(prepared_serialization &&) = default;
        prepared_serialization &operator=(prepared_serialization &&) = delete;
        prepared_serialization(const prepared_serialization &) = delete;
        prepared_serialization &operator=(const prepared_serialization &) = delete;

        [[nodiscard]] static std::expected<prepared_serialization, error> prepare(
            const T &value, const serialization_context &context) requires (!detail::root_codec<T>::uses_recorder) {
            return prepare_with(value, context);
        }

        [[nodiscard]] static std::expected<prepared_serialization, error> prepare(
            encoding_recorder &recorder, const T &value, const serialization_context &context) {
            return prepare_with(value, context, recorder);
        }

        static std::expected<prepared_serialization, error> prepare(const T &&, const serialization_context &) = delete;
        static std::expected<prepared_serialization, error> prepare(
            encoding_recorder &, const T &&, const serialization_context &) = delete;

        [[nodiscard]] szt size() const noexcept { return operation_.plan().size; }

        template<std::size_t Extent>
            requires detail::encoding_destination<T, Extent, Mode>
        [[nodiscard]] detail::prepared_encode_result<T, Extent, Mode> encode(std::span<std::byte, Extent> destination) const {
            return detail::encode_to_span(operation_, *value_, destination);
        }
    };

    template<serializable T, schema_storage Mode = schema_storage::embedded>
        requires (!detail::root_encode_extent<T, Mode>.is_dynamic())
    [[nodiscard]] consteval szt serialized_size() noexcept {
        return detail::root_encode_extent<T, Mode>.value;
    }

    template<schema_storage Mode = schema_storage::embedded, serializable T>
        requires (!detail::root_encode_extent<T, Mode>.is_dynamic())
    [[nodiscard]] constexpr szt serialized_size(
        const T &, const serialization_context &) noexcept {
        return serialized_size<T, Mode>();
    }

    template<schema_storage Mode = schema_storage::embedded, serializable T>
        requires (detail::root_encode_extent<T, Mode>.is_dynamic() && !detail::root_codec<T>::uses_recorder)
    [[nodiscard]] std::expected<szt, error> serialized_size(
        const T &value, const serialization_context &context) {
        detail::encoding_operation<T, Mode> operation;
        return detail::measure_with(operation, value, context);
    }

    template<schema_storage Mode = schema_storage::embedded, serializable T>
    [[nodiscard]] auto serialized_size(
        encoding_recorder &recorder, const T &value, const serialization_context &context) {
        if constexpr (!detail::root_encode_extent<T, Mode>.is_dynamic()) {
            return serialized_size<T, Mode>();
        } else {
            detail::encoding_operation<T, Mode> operation{recorder};
            return detail::measure_with(operation, value, context);
        }
    }

    template<schema_storage Mode = schema_storage::embedded, serializable T, std::size_t Extent>
        requires (detail::encoding_destination<T, Extent, Mode> && !detail::root_codec<T>::uses_recorder)
    [[nodiscard]] detail::serialize_result<T, Extent, Mode> serialize(
        const T &value, std::span<std::byte, Extent> destination, const srl_context<> &context) {
        detail::encoding_operation<T, Mode> operation;
        return detail::serialize_with(operation, value, destination, context.settings());
    }

    template<schema_storage Mode = schema_storage::embedded, serializable T, std::size_t Extent>
        requires detail::encoding_destination<T, Extent, Mode>
    [[nodiscard]] detail::serialize_result<T, Extent, Mode> serialize(
        const T &value, std::span<std::byte, Extent> destination, const srl_context<true> &context) {
        detail::encoding_operation<T, Mode> operation{context.recorder()};
        return detail::serialize_with(operation, value, destination, context.settings());
    }

    template<schema_storage Mode = schema_storage::embedded, serializable T>
        requires (!detail::root_codec<T>::uses_recorder)
    [[nodiscard]] error serialize(
        const T &value, byte_buffer &destination, const srl_context<> &context) {
        detail::encoding_operation<T, Mode> operation;
        return detail::serialize_with(operation, value, destination, context.settings());
    }

    template<schema_storage Mode = schema_storage::embedded, serializable T>
    [[nodiscard]] error serialize(
        const T &value, byte_buffer &destination, const srl_context<true> &context) {
        detail::encoding_operation<T, Mode> operation{context.recorder()};
        return detail::serialize_with(operation, value, destination, context.settings());
    }

    namespace detail {
        template<typename Root>
        auto open_document(std::span<const std::byte> bytes) {
            return wire_document::read(bytes, mapping_layout_for<Root::tree>.fingerprint);
        }

        template<typename Root, typename T>
        error decode_document(const wire_document &document, T &target, const serialization_context &context) {
            constexpr auto local = mapping_layout_for<Root::tree>.nodes[Root::node].definition;
            auto entry = defined_entry::parse(document.root, document, local);
            if (!entry) { return entry.error().rebase(document.root_offset); }
            return Root::decode_entry(*entry, target, context, document).rebase(document.root_offset);
        }
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
        using root = detail::root_codec<T>;
        auto document = detail::open_document<root>(bytes);
        if (!document) { return document.error(); }
        auto temporary = [&] {
            if constexpr (root::descriptor.kind() == schema_kind::container
                && requires { transaction_traits<codec_value_type<T>>::stage(out); }) {
                return transaction_traits<codec_value_type<T>>::stage(out);
            } else {
                return transaction_traits<codec_value_type<T>>::clone(out);
            }
        }();
        if (auto decoded = detail::decode_document<root>(*document, temporary, context); decoded) {
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
        using root = detail::root_codec<T[Size]>;
        auto document = detail::open_document<root>(bytes);
        if (!document) { return document.error(); }
        auto temporary = [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return std::array<T, Size>{transaction_traits<T>::clone(out[Indices])...};
        }(std::make_index_sequence<Size>{});
        if (auto decoded = detail::decode_document<detail::root_codec<decltype(temporary)>>(*document, temporary, context); decoded) {
            return decoded;
        }
        for (std::size_t index = 0; index != Size; ++index) {
            transaction_traits<T>::commit(out[index], temporary[index]);
        }
        return {};
    }
}
