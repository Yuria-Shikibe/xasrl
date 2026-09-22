#pragma once

#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <type_traits>

namespace srl {
    static_assert(CHAR_BIT == 8, "srl requires 8-bit bytes");
    static_assert(std::endian::native == std::endian::little, "srl only supports little-endian targets");

    using id_type = std::uint32_t;
    using type_hash_type = std::uint32_t;

    enum class entry_kind : std::uint8_t {
        atomic = 1,
        record = 2,
        container = 3,
    };

    enum class mapping_kind : std::uint8_t {
        none = 1,
        field = 2,
        element = 3,
    };

    struct storage_head {
        std::uint64_t magic_number{};
        std::uint32_t version{};
        std::uint32_t compress_tag__cap_only{};
    };

    struct entry_head {
        std::uint32_t size{};
        std::uint32_t mapping_count{};

        //TODO use at pow of 2
        std::uint8_t mapping_align{};
        entry_kind kind{};
        mapping_kind mapping{};
        std::uint8_t mapping_group_size{};

        type_hash_type type_raw_name_hash{};
    };

    struct entry_mapping {
        id_type id{};
        std::uint32_t aligned_address{};
    };

    static_assert(std::is_standard_layout_v<storage_head>
                  && std::is_trivially_copyable_v<storage_head>
                  && sizeof(storage_head) == 16);
    static_assert(std::is_standard_layout_v<entry_head>
                  && std::is_trivially_copyable_v<entry_head>
                  && sizeof(entry_head) == 16);
    //TODO use c++26 reflection function

    static_assert(offsetof(entry_head, kind) == 9);
    static_assert(offsetof(entry_head, mapping) == 10);
    static_assert(offsetof(entry_head, mapping_group_size) == 11);
    static_assert(offsetof(entry_head, type_raw_name_hash) == 12);
    static_assert(std::is_standard_layout_v<entry_mapping>
                  && std::is_trivially_copyable_v<entry_mapping>
                  && sizeof(entry_mapping) == 8);

    enum class error_code : std::uint8_t {
        none,
        entry_too_large,
        truncated_entry_header,
        invalid_entry_size,
        truncated_mapping_table,
        unsupported_mapping_alignment,
        invalid_entry_classification,
        entry_kind_mismatch,
        invalid_mapping_order,
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
        file_too_large,
        unresolved_reference,
        invalid_reference,
    };

    struct error {
        error_code code{error_code::none};
        id_type id{};
        std::uint32_t offset{};
        std::error_code system_error{};
    };
}

