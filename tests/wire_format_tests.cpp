#include "test_support.hpp"

namespace srl_test {
    void test_mapping_layout() {
        srl::byte_buffer bytes{};
        const auto bytes_status = srl::serialize(Pair{
            .first = 0x11223344,
            .second = 0x55667788,
        }, bytes, default_context);
        require(!bytes_status, "Pair packing failed");
        require(bytes.size() == 56, "Pair entry size changed");
        require(get_u64(bytes, 0) == 56, "entry head size is incorrect");
        require((get_u32(bytes, 8) & 0xffffffu) == 2, "mapping count is incorrect");
        require(std::to_integer<std::uint8_t>((bytes)[11])
                    == std::to_underlying(srl::entry_kind::record),
                "record entry classification is incorrect");
        constexpr auto pair_wire_type = srl::schema<Pair>::node(srl::schema<Pair>::value).wire_type;
        require(get_u32(bytes, 12) == pair_wire_type,
                "entry type hash is incorrect");
        constexpr auto integer_wire_type = srl::schema<int>::node(srl::schema<int>::value).wire_type;
        require(get_u32(bytes, 16) == 1 && get_u32(bytes, 20) == integer_wire_type && get_u64(bytes, 24) == 0,
                "first mapping is incorrect");
        require(get_u32(bytes, 32) == 2 && get_u32(bytes, 36) == integer_wire_type && get_u64(bytes, 40) == 4,
                "second mapping is incorrect");
        require(get_u32(bytes, 48) == 0x11223344u
                && get_u32(bytes, 52) == 0x55667788u,
                "content is not canonical little endian");

        auto entry = srl::detail::parse_entry(bytes.span());
        require(entry.has_value() && entry->head.mapping_count() == 2,
                "Pair mapping table did not parse");

        const auto direct_size = srl::serialized_size(Pair{.first = 0x11223344, .second = 0x55667788}, default_context);
        require(direct_size == bytes.size(),
                "serialized_size did not match serialization output");
        srl::byte_buffer direct;
        direct.resize(direct_size);
        require(!srl::serialize(Pair{.first = 0x11223344, .second = 0x55667788}, direct.span(), default_context)
                && direct == bytes,
                "serialization did not write directly into the supplied span");
    }

    void test_compatible_views() {
        srl::byte_buffer full{};
        const auto full_status = srl::serialize(Pair{.first = 10, .second = 20}, full, default_context);
        require(!full_status, "full view packing failed");
        FirstOnly subset{};
        const auto subset_status = srl::deserialize(full.span(), subset, default_context);
        require(!subset_status && subset.first == 10,
                "unknown stored fields must be ignored");

        srl::byte_buffer partial{};
        const auto partial_status = srl::serialize(FirstOnly{.first = 10}, partial, default_context);
        require(!partial_status, "partial view packing failed");
        Pair defaulted{.first = 99, .second = 99};
        require_error(srl::deserialize(partial.span(), defaulted, default_context),
            srl::error_code::missing_field, "missing required field must be rejected");
        require(defaulted.first == 99 && defaulted.second == 99,
            "missing field failure must not modify the target");
    }

    void test_container_round_trips() {
        const std::vector<int> vector{1, 2, 3, 5, 8};
        srl::byte_buffer vector_bytes{};
        const auto vector_bytes_status = srl::serialize(vector, vector_bytes, default_context);
        require(!vector_bytes_status, "vector packing failed");
        require(std::to_integer<std::uint8_t>((vector_bytes)[11])
                    == std::to_underlying(srl::entry_kind::sequence),
                "single-value container classification is incorrect");
        std::vector<int> vector_copy{};
        const auto vector_copy_status = srl::deserialize(vector_bytes.span(), vector_copy, default_context);
        require(!vector_copy_status && vector_copy == vector,
                "vector round trip failed");

        std::list<int> list_copy{};
        const auto list_copy_status = srl::deserialize(vector_bytes.span(), list_copy, default_context);
        require(!list_copy_status && std::ranges::equal(list_copy, vector),
                "compatible vector-to-list decode failed");

        const std::forward_list<int> forward{4, 3, 2, 1};
        srl::byte_buffer forward_bytes{};
        const auto forward_bytes_status = srl::serialize(forward, forward_bytes, default_context);
        require(!forward_bytes_status, "forward_list packing failed");
        std::forward_list<int> forward_copy{};
        const auto forward_copy_status = srl::deserialize(forward_bytes.span(), forward_copy, default_context);
        require(!forward_copy_status && forward_copy == forward,
                "forward_list order changed");

        const std::array<int, 3> array{9, 8, 7};
        srl::byte_buffer array_bytes{};
        const auto array_bytes_status = srl::serialize(array, array_bytes, default_context);
        require(!array_bytes_status, "array packing failed");
        std::array<int, 3> array_copy{};
        const auto array_copy_status = srl::deserialize(array_bytes.span(), array_copy, default_context);
        require(!array_copy_status && array_copy == array,
                "array round trip failed");
        std::array<int, 2> rejected_value_2{};
        require_error(srl::deserialize(array_bytes.span(), rejected_value_2, default_context),
                      srl::error_code::fixed_container_size_mismatch,
                      "array element-count mismatch was accepted");

        const FixedSequence<3> fixed{{7, 5, 3}};
        srl::byte_buffer fixed_bytes{};
        const auto fixed_bytes_status = srl::serialize(fixed, fixed_bytes, default_context);
        require(!fixed_bytes_status, "automatically detected fixed container packing failed");
        FixedSequence<3> fixed_copy{};
        const auto fixed_copy_status = srl::deserialize(fixed_bytes.span(), fixed_copy, default_context);
        require(!fixed_copy_status && fixed_copy == fixed,
                "automatically detected fixed container round trip failed");
        FixedSequence<2> rejected_value_3{};
        require_error(srl::deserialize(fixed_bytes.span(), rejected_value_3, default_context),
                      srl::error_code::fixed_container_size_mismatch,
                      "automatically detected fixed container mismatch was accepted");

        int native_array[3]{6, 4, 2};
        srl::byte_buffer native_array_bytes{};
        const auto native_array_bytes_status = srl::serialize(native_array, native_array_bytes, default_context);
        require(!native_array_bytes_status, "native array packing failed");
        int native_array_copy[3]{};
        require(!srl::deserialize(native_array_bytes.span(), native_array_copy, default_context)
                && std::ranges::equal(native_array, native_array_copy),
                "native array round trip failed");

        const RawArrayField raw_field{.values = {3, 1, 4}};
        srl::byte_buffer raw_field_bytes{};
        const auto raw_field_bytes_status = srl::serialize(raw_field, raw_field_bytes, default_context);
        require(!raw_field_bytes_status, "native array field packing failed");
        RawArrayField raw_field_copy{};
        const auto raw_field_copy_status = srl::deserialize(raw_field_bytes.span(), raw_field_copy, default_context);
        require(!raw_field_copy_status
                && std::ranges::equal(raw_field.values, raw_field_copy.values),
                "native array field round trip failed");

        const std::vector<std::vector<int>> nested{{1, 2}, {}, {3, 4, 5}};
        srl::byte_buffer nested_bytes{};
        const auto nested_bytes_status = srl::serialize(nested, nested_bytes, default_context);
        require(!nested_bytes_status, "nested container packing failed");
        std::vector<std::vector<int>> nested_copy{};
        const auto nested_copy_status = srl::deserialize(nested_bytes.span(), nested_copy, default_context);
        require(!nested_copy_status && nested_copy == nested,
                "nested container round trip failed");

        const std::vector<bool> bits{true, false, true, true};
        srl::byte_buffer bit_bytes{};
        const auto bit_bytes_status = srl::serialize(bits, bit_bytes, default_context);
        require(!bit_bytes_status, "vector<bool> packing failed");
        std::vector<bool> bit_copy{};
        const auto bit_copy_status = srl::deserialize(bit_bytes.span(), bit_copy, default_context);
        require(!bit_copy_status && bit_copy == bits,
                "vector<bool> proxy elements failed to round trip");

        TrackingSequence tracking{.values = {6, 7, 8}};
        srl::byte_buffer tracking_bytes{};
        const auto tracking_bytes_status = srl::serialize(tracking, tracking_bytes, default_context);
        require(!tracking_bytes_status, "custom container packing failed");
        TrackingSequence tracking_copy{};
        const auto tracking_copy_status = srl::deserialize(tracking_bytes.span(), tracking_copy, default_context);
        require(!tracking_copy_status
                && tracking_copy.values == tracking.values
                && tracking_copy.reserved == tracking.values.size(),
                "custom traits or reserve optimization failed");

        const std::map<int, std::string> map{{1, "one"}, {2, "two"}};
        srl::byte_buffer map_bytes{};
        const auto map_bytes_status = srl::serialize(map, map_bytes, default_context);
        require(!map_bytes_status, "map packing failed");
        require((get_u32(map_bytes, 8) & 0xffffffu) == map.size() * 2
                && std::to_integer<std::uint8_t>((map_bytes)[11]) == std::to_underlying(srl::entry_kind::map),
                "map key/value slots were not grouped in pairs");
        std::unordered_map<int, std::string> unordered_copy{};
        const auto unordered_copy_status = srl::deserialize(map_bytes.span(), unordered_copy, default_context);
        require(!unordered_copy_status
                && unordered_copy.at(1) == "one"
                && unordered_copy.at(2) == "two",
                "map-to-unordered_map decode failed");

        const std::vector<int> duplicates{3, 3, 5};
        srl::byte_buffer duplicate_bytes{};
        const auto duplicate_bytes_status = srl::serialize(duplicates, duplicate_bytes, default_context);
        require(!duplicate_bytes_status, "duplicate sequence packing failed");
        std::set<int> deduplicated{};
        const auto deduplicated_status = srl::deserialize(duplicate_bytes.span(), deduplicated, default_context);
        require(!deduplicated_status && deduplicated == std::set<int>{3, 5},
                "default unique-container deduplication failed");
        std::set<int> rejected_value_4{};
        require_error(srl::deserialize(duplicate_bytes.span(), rejected_value_4, rejecting_context),
            srl::error_code::duplicate_container_element,
            "root duplicate rejection was not enforced");

        srl::byte_buffer field_bytes{};
        const auto field_bytes_status = srl::serialize(VectorField{.values = duplicates}, field_bytes, default_context);
        require(!field_bytes_status, "container field packing failed");
        RejectingSetField rejected_value_5{};
        require_error(srl::deserialize(field_bytes.span(), rejected_value_5, default_context),
                      srl::error_code::duplicate_container_element,
                      "field container duplicate tag was not enforced");
    }

    void test_container_entry_size_limit() {
        using codec = srl::detail::root_codec<std::vector<int>>;

        codec::encode_plan plan;
        plan.payload.size = srl::detail::maximum_entry_size
            - srl::detail::entry_mapping_wire_size;

        const auto status = plan.prepare(std::vector<int>{1}, default_context);
        require(status.code == srl::error_code::entry_too_large,
                "container encoding accepted an entry too large for the wire size field");
    }

    void test_malformed_input() {
        srl::byte_buffer pair_bytes{};
        const auto pair_bytes_status = srl::serialize(Pair{.first = 10, .second = 20}, pair_bytes, default_context);
        require(!pair_bytes_status, "Pair packing failed");

        auto oversized = pair_bytes;
        set_u64(oversized, 0, oversized.size() + 1);
        Pair rejected_value_6{};
        require_error(srl::deserialize(oversized.span(), rejected_value_6, default_context),
                      srl::error_code::invalid_entry_size,
                      "oversized entry was accepted");

        auto trailing = pair_bytes;
        trailing.push_back(std::byte{});
        Pair rejected_value_7{};
        require_error(srl::deserialize(trailing.span(), rejected_value_7, default_context),
                      srl::error_code::trailing_bytes,
                      "trailing entry bytes were accepted");

        auto bad_reserved = pair_bytes;
        bad_reserved[20] = std::byte{2};
        Pair rejected_value_8{};
        require_error(srl::deserialize(bad_reserved.span(), rejected_value_8, default_context),
                      srl::error_code::type_mismatch,
                      "invalid field wire type was accepted");

        auto bad_classification = pair_bytes;
        bad_classification[11] = std::byte{};
        Pair rejected_value_9{};
        require_error(srl::deserialize(bad_classification.span(), rejected_value_9, default_context),
                      srl::error_code::invalid_entry_classification,
                      "invalid entry classification was accepted");

        auto duplicate = pair_bytes;
        set_u32(duplicate, 32, 1);
        Pair rejected_value_10{};
        require_error(srl::deserialize(duplicate.span(), rejected_value_10, default_context),
                      srl::error_code::invalid_mapping_order,
                      "duplicate field id was accepted");

        auto bad_address = pair_bytes;
        set_u64(bad_address, 24, std::numeric_limits<srl::szt>::max());
        Pair rejected_value_11{};
        require_error(srl::deserialize(bad_address.span(), rejected_value_11, default_context),
                      srl::error_code::payload_out_of_bounds,
                      "out-of-bounds mapping was accepted");

        Pair rejected_value_12{};
        require_error(srl::deserialize(pair_bytes.span().first(pair_bytes.size() - 1), rejected_value_12, default_context),
                      srl::error_code::invalid_entry_size,
                      "truncated entry was accepted");

        srl::byte_buffer bool_bytes{};
        const auto bool_bytes_status = srl::serialize(true, bool_bytes, default_context);
        require(!bool_bytes_status, "bool packing failed");
        auto invalid_bool = bool_bytes;
        invalid_bool[srl::detail::entry_head_wire_size] = std::byte{2};
        bool rejected_value_13{};
        require_error(srl::deserialize(invalid_bool.span(), rejected_value_13, default_context),
                      srl::error_code::invalid_bool,
                      "invalid bool payload was accepted");

        srl::byte_buffer enum_bytes{};
        const auto enum_bytes_status = srl::serialize(activity::sleeping, enum_bytes, default_context);
        require(!enum_bytes_status, "enum packing failed");
        auto invalid_enum = enum_bytes;
        invalid_enum[srl::detail::entry_head_wire_size] = std::byte{7};
        activity rejected_value_14{};
        require_error(srl::deserialize(invalid_enum.span(), rejected_value_14, default_context),
                      srl::error_code::invalid_enum_value,
                      "invalid enum payload was accepted");

        srl::byte_buffer int_bytes{};
        const auto int_bytes_status = srl::serialize(42, int_bytes, default_context);
        require(!int_bytes_status, "integer packing failed");
        unsigned int rejected_value_15{};
        require_error(srl::deserialize(int_bytes.span(), rejected_value_15, default_context),
                      srl::error_code::type_mismatch,
                      "leaf root type mismatch was accepted");

        srl::byte_buffer map_bytes{};
        const auto map_bytes_status = srl::serialize(std::map<int, int>{{1, 2}, {3, 4}}, map_bytes, default_context);
        require(!map_bytes_status, "map packing failed");
        auto incomplete_pair = map_bytes;
        incomplete_pair[8] = std::byte{3};
        std::map<int, int> rejected_value_16{};
        require_error(srl::deserialize(incomplete_pair.span(), rejected_value_16, default_context),
                      srl::error_code::invalid_container_shape,
                      "incomplete key/value mapping group was accepted");

        auto wrong_container_id = map_bytes;
        set_u32(wrong_container_id, 16, 4);
        std::map<int, int> rejected_value_17{};
        require_error(srl::deserialize(wrong_container_id.span(), rejected_value_17, default_context),
                      srl::error_code::invalid_container_shape,
                      "non-contiguous container mapping ids were accepted");
    }


TEST(WireFormat, MappingLayout) {
    test_mapping_layout();
}

TEST(WireFormat, CompatibleViews) {
    test_compatible_views();
}

TEST(ContainerSerialization, RoundTrips) {
    test_container_round_trips();
    test_container_entry_size_limit();
}

TEST(MalformedInput, RejectsInvalidBytes) {
    test_malformed_input();
}
}
