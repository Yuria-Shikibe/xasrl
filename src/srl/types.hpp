#pragma once

#include <meta>

#include <bit>
#include <climits>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <type_traits>

namespace srl {
    static_assert(CHAR_BIT == 8, "srl requires 8-bit bytes");
    static_assert(std::endian::native == std::endian::little, "srl only supports little-endian targets");

    using szt = std::uint64_t;
    using id_type = std::uint32_t;
    using type_hash_type = std::uint32_t;

    enum class entry_kind : std::uint8_t {
        leaf = 1,
        stream = 2,
        record = 3,
        sequence = 4,
        map = 5,
    };

    class entry_metadata {
        // Low 24 bits: mapping count; high 8 bits: mutually exclusive wire kind.
        std::uint32_t bits_{};

    public:
        static constexpr std::uint32_t max_mapping_count = 0x00ffffffu;

        constexpr entry_metadata() = default;

        constexpr entry_metadata(szt count, entry_kind kind) {
            if (count > max_mapping_count) {
                throw std::out_of_range("entry mapping count exceeds 24 bits");
            }
            bits_ = static_cast<std::uint32_t>(count) | (static_cast<std::uint32_t>(kind) << 24);
        }

        [[nodiscard]] constexpr std::uint32_t mapping_count() const noexcept {
            return bits_ & max_mapping_count;
        }

        [[nodiscard]] constexpr entry_kind kind() const noexcept {
            return static_cast<entry_kind>(bits_ >> 24);
        }
    };

    struct storage_head {
        std::uint64_t magic_number{};
        std::uint32_t version{};
        std::uint32_t compress_tag__cap_only{};
    };

    struct entry_head {
        szt size{};
        entry_metadata metadata{};
        type_hash_type wire_type{};

        [[nodiscard]] constexpr std::uint32_t mapping_count() const noexcept {
            return metadata.mapping_count();
        }

        [[nodiscard]] constexpr entry_kind kind() const noexcept {
            return metadata.kind();
        }
    };

    static_assert(std::meta::is_standard_layout_type(^^storage_head)
                  && std::meta::is_trivially_copyable_type(^^storage_head)
                  && sizeof(storage_head) == 16);
    static_assert(std::meta::is_standard_layout_type(^^entry_head)
                  && std::meta::is_trivially_copyable_type(^^entry_head)
                  && sizeof(entry_head) == 16);

    static_assert(sizeof(entry_metadata) == 4);
    static_assert(offsetof(entry_head, size) == 0);
    static_assert(offsetof(entry_head, metadata) == 8);
    static_assert(offsetof(entry_head, wire_type) == 12);

    enum class error_code : std::uint8_t {
        none,
        entry_too_large,
        too_many_mappings,
        resource_limit_exceeded,
        truncated_entry_header,
        invalid_entry_size,
        truncated_mapping_table,
        invalid_entry_classification,
        entry_kind_mismatch,
        invalid_mapping_order,
        invalid_mapping_reference,
        invalid_container_shape,
        duplicate_container_element,
        fixed_container_size_mismatch,
        payload_out_of_bounds,
        missing_field,
        type_mismatch,
        invalid_payload_shape,
        invalid_bool,
        invalid_enum_value,
        trailing_bytes,
        invalid_storage_magic,
        unsupported_storage_version,
        unsupported_compression,
        io_open_failed,
        io_seek_failed,
        io_read_failed,
        io_write_failed,
        io_flush_failed,
        io_commit_failed,
        file_too_large,
        unresolved_reference,
        invalid_reference,
    };

    //TODO make the error struct smaller
    struct error {
        error_code code{error_code::none};
        std::optional<id_type> id;
        szt offset{};
        std::optional<std::error_code> system_error;

        [[nodiscard]] constexpr explicit operator bool() const noexcept {
            return code != error_code::none;
        }

        constexpr error& set_id(std::optional<id_type> value) noexcept {
            if (!id) {
                id = value;
            }
            return *this;
        }

        // Codec errors are relative to their input. Each enclosing codec adds
        // its payload origin exactly once on the error path.
        constexpr error& rebase(szt origin) noexcept {
            offset += origin;
            return *this;
        }
    };


    struct always_success {
        [[nodiscard]] constexpr explicit operator bool() const noexcept {
            return false;
        }

        constexpr always_success& set_id(std::optional<id_type>) noexcept { return *this; }

        constexpr always_success& rebase(szt) noexcept { return *this; }

        constexpr explicit(false) operator error() const noexcept {
            return {};
        }
    };

    template<typename T>
    concept codec_status = std::same_as<T, error> || std::same_as<T, always_success>;

    namespace detail {
        template<bool AlwaysSuccessful>
        using status_type = [:AlwaysSuccessful ? ^^always_success : ^^error:];

        template<codec_status... Status>
        using combined_status = status_type<(std::same_as<Status, always_success> && ...)>;
    }

}
