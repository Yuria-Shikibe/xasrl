#include <memory_resource>

#include "test_support.hpp"

namespace srl_test {
    void test_mapping_layout() {
        xasrl::byte_buffer bytes{};
        const auto bytes_status = xasrl::serialize(Pair{
            .first = 0x11223344,
            .second = 0x55667788,
        }, bytes, default_context);
        require(!bytes_status, "Pair packing failed");
        const auto doc = open_document(bytes.span());
        require(bytes.size() == 84 && doc.root_offset() == 16, "Pair document size changed");
        require(doc.entry.head().size == 68 && doc.entry.head().mapping_count() == 2, "wrong entry prefix");
        require(doc.entry.head().kind() == xasrl::entry_kind::record, "record classification is incorrect");
        constexpr auto pair_wire_type = xasrl::schema(^^Pair).value_node().wire_type();
        constexpr auto integer_wire_type = xasrl::schema(^^int).value_node().wire_type();
        const auto first = doc.at(0);
        const auto second = doc.at(1);
        require(doc.entry.head().wire_type == pair_wire_type && first.id == 1 && second.id == 2,
                "entry type or field IDs are incorrect");
        require(first.wire_type == integer_wire_type && second.wire_type == integer_wire_type,
                "field types are incorrect");
        require(first.offset == 76 && second.offset == 80 && get_u32(bytes, first.offset) == 0x11223344u
                && get_u32(bytes, second.offset) == 0x55667788u, "payload is not canonical little endian");
        const auto direct_size = xasrl::serialized_size(Pair{.first = 0x11223344, .second = 0x55667788}, default_context);
        require(direct_size == bytes.size(),
                "serialized_size did not match serialization output");
        xasrl::byte_buffer direct;
        direct.resize(direct_size);
        require(!xasrl::serialize(Pair{.first = 0x11223344, .second = 0x55667788}, direct.span(), default_context)
                && direct == bytes,
                "serialization did not write directly into the supplied span");
    }

    void test_compatible_views() {
        xasrl::byte_buffer full{};
        const auto full_status = xasrl::serialize(Pair{.first = 10, .second = 20}, full, default_context);
        require(!full_status, "full view packing failed");
        FirstOnly subset{};
        const auto subset_status = xasrl::deserialize(full.span(), subset, default_context);
        require(!subset_status && subset.first == 10,
                "unknown stored fields must be ignored");

        xasrl::byte_buffer partial{};
        const auto partial_status = xasrl::serialize(FirstOnly{.first = 10}, partial, default_context);
        require(!partial_status, "partial view packing failed");
        Pair defaulted{.first = 99, .second = 99};
        require_error(xasrl::deserialize(partial.span(), defaulted, default_context),
            xasrl::error_code::missing_field, "missing required field must be rejected");
        require(defaulted.first == 99 && defaulted.second == 99,
            "missing field failure must not modify the target");
    }

    void test_container_round_trips() {
        const std::vector<int> vector{1, 2, 3, 5, 8};
        xasrl::byte_buffer vector_bytes{};
        const auto vector_bytes_status = xasrl::serialize(vector, vector_bytes, default_context);
        require(!vector_bytes_status, "vector packing failed");
        require(std::to_integer<std::uint8_t>((vector_bytes)[27])
                    == std::to_underlying(xasrl::entry_kind::sequence),
                "single-value container classification is incorrect");
        std::vector<int> vector_copy{};
        const auto vector_copy_status = xasrl::deserialize(vector_bytes.span(), vector_copy, default_context);
        require(!vector_copy_status && vector_copy == vector,
                "vector round trip failed");

        std::list<int> list_copy{};
        const auto list_copy_status = xasrl::deserialize(vector_bytes.span(), list_copy, default_context);
        require(!list_copy_status && std::ranges::equal(list_copy, vector),
                "compatible vector-to-list decode failed");

        const std::forward_list<int> forward{4, 3, 2, 1};
        xasrl::byte_buffer forward_bytes{};
        const auto forward_bytes_status = xasrl::serialize(forward, forward_bytes, default_context);
        require(!forward_bytes_status, "forward_list packing failed");
        std::forward_list<int> forward_copy{};
        const auto forward_copy_status = xasrl::deserialize(forward_bytes.span(), forward_copy, default_context);
        require(!forward_copy_status && forward_copy == forward,
                "forward_list order changed");

        const std::array<int, 3> array{9, 8, 7};
        xasrl::byte_buffer array_bytes{};
        const auto array_bytes_status = xasrl::serialize(array, array_bytes, default_context);
        require(!array_bytes_status, "array packing failed");
        std::array<int, 3> array_copy{};
        const auto array_copy_status = xasrl::deserialize(array_bytes.span(), array_copy, default_context);
        require(!array_copy_status && array_copy == array,
                "array round trip failed");
        std::array<int, 2> rejected_value_2{};
        require_error(xasrl::deserialize(array_bytes.span(), rejected_value_2, default_context),
                      xasrl::error_code::fixed_container_size_mismatch,
                      "array element-count mismatch was accepted");

        const FixedSequence<3> fixed{{7, 5, 3}};
        xasrl::byte_buffer fixed_bytes{};
        const auto fixed_bytes_status = xasrl::serialize(fixed, fixed_bytes, default_context);
        require(!fixed_bytes_status, "automatically detected fixed container packing failed");
        FixedSequence<3> fixed_copy{};
        const auto fixed_copy_status = xasrl::deserialize(fixed_bytes.span(), fixed_copy, default_context);
        require(!fixed_copy_status && fixed_copy == fixed,
                "automatically detected fixed container round trip failed");
        FixedSequence<2> rejected_value_3{};
        require_error(xasrl::deserialize(fixed_bytes.span(), rejected_value_3, default_context),
                      xasrl::error_code::fixed_container_size_mismatch,
                      "automatically detected fixed container mismatch was accepted");

        int native_array[3]{6, 4, 2};
        xasrl::byte_buffer native_array_bytes{};
        const auto native_array_bytes_status = xasrl::serialize(native_array, native_array_bytes, default_context);
        require(!native_array_bytes_status, "native array packing failed");
        int native_array_copy[3]{};
        require(!xasrl::deserialize(native_array_bytes.span(), native_array_copy, default_context)
                && std::ranges::equal(native_array, native_array_copy),
                "native array round trip failed");

        const RawArrayField raw_field{.values = {3, 1, 4}};
        xasrl::byte_buffer raw_field_bytes{};
        const auto raw_field_bytes_status = xasrl::serialize(raw_field, raw_field_bytes, default_context);
        require(!raw_field_bytes_status, "native array field packing failed");
        RawArrayField raw_field_copy{};
        const auto raw_field_copy_status = xasrl::deserialize(raw_field_bytes.span(), raw_field_copy, default_context);
        require(!raw_field_copy_status
                && std::ranges::equal(raw_field.values, raw_field_copy.values),
                "native array field round trip failed");

        const std::vector<std::vector<int>> nested{{1, 2}, {}, {3, 4, 5}};
        xasrl::byte_buffer nested_bytes{};
        xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
        const auto nested_bytes_status = xasrl::serialize(nested, nested_bytes, xasrl::srl_context{default_context, recorder});
        require(!nested_bytes_status, "nested container packing failed");
        const auto nested_doc = open_document(nested_bytes.span());
        require(nested_bytes.size() == 212 && nested_doc.at(0).bytes.size() == 52
                && nested_doc.at(1).bytes.size() == 44 && nested_doc.at(2).bytes.size() == 56,
                "nested container entry lengths are incorrect");
        std::vector<std::vector<int>> nested_copy{};
        const auto nested_copy_status = xasrl::deserialize(nested_bytes.span(), nested_copy, default_context);
        require(!nested_copy_status && nested_copy == nested,
                "nested container round trip failed");

        const std::vector<bool> bits{true, false, true, true};
        xasrl::byte_buffer bit_bytes{};
        const auto bit_bytes_status = xasrl::serialize(bits, bit_bytes, default_context);
        require(!bit_bytes_status, "vector<bool> packing failed");
        std::vector<bool> bit_copy{};
        const auto bit_copy_status = xasrl::deserialize(bit_bytes.span(), bit_copy, default_context);
        require(!bit_copy_status && bit_copy == bits,
                "vector<bool> proxy elements failed to round trip");

        TrackingSequence tracking{.values = {6, 7, 8}};
        xasrl::byte_buffer tracking_bytes{};
        const auto tracking_bytes_status = xasrl::serialize(tracking, tracking_bytes, default_context);
        require(!tracking_bytes_status, "custom container packing failed");
        TrackingSequence tracking_copy{};
        const auto tracking_copy_status = xasrl::deserialize(tracking_bytes.span(), tracking_copy, default_context);
        require(!tracking_copy_status
                && tracking_copy.values == tracking.values
                && tracking_copy.reserved == tracking.values.size(),
                "custom traits or reserve optimization failed");

        const std::map<int, std::string> map{{1, "one"}, {2, "two"}};
        xasrl::byte_buffer map_bytes{};
        const auto map_bytes_status = xasrl::serialize(map, map_bytes, xasrl::srl_context{default_context, recorder});
        require(!map_bytes_status, "map packing failed");
        require((get_u32(map_bytes, 24) & 0xffffffu) == map.size() * 2
                && std::to_integer<std::uint8_t>((map_bytes)[27]) == std::to_underlying(xasrl::entry_kind::map),
                "map key/value slots were not grouped in pairs");
        std::unordered_map<int, std::string> unordered_copy{};
        const auto unordered_copy_status = xasrl::deserialize(map_bytes.span(), unordered_copy, default_context);
        require(!unordered_copy_status
                && unordered_copy.at(1) == "one"
                && unordered_copy.at(2) == "two",
                "map-to-unordered_map decode failed");

        const std::vector<int> duplicates{3, 3, 5};
        xasrl::byte_buffer duplicate_bytes{};
        const auto duplicate_bytes_status = xasrl::serialize(duplicates, duplicate_bytes, default_context);
        require(!duplicate_bytes_status, "duplicate sequence packing failed");
        std::set<int> deduplicated{};
        const auto deduplicated_status = xasrl::deserialize(duplicate_bytes.span(), deduplicated, default_context);
        require(!deduplicated_status && deduplicated == std::set<int>{3, 5},
                "default unique-container deduplication failed");
        std::set<int> rejected_value_4{};
        require_error(xasrl::deserialize(duplicate_bytes.span(), rejected_value_4, rejecting_context),
            xasrl::error_code::duplicate_container_element,
            "root duplicate rejection was not enforced");

        xasrl::byte_buffer field_bytes{};
        const auto field_bytes_status = xasrl::serialize(VectorField{.values = duplicates}, field_bytes, default_context);
        require(!field_bytes_status, "container field packing failed");
        RejectingSetField rejected_value_5{};
        require_error(xasrl::deserialize(field_bytes.span(), rejected_value_5, default_context),
                      xasrl::error_code::duplicate_container_element,
                      "field container duplicate tag was not enforced");
    }

    void test_container_entry_size_limit() {
        constexpr auto maximum_payload = xasrl::detail::maximum_entry_size - xasrl::detail::entry_head_wire_size;
        const std::array<xasrl::szt, 1> exact{maximum_payload};
        const auto boundary = xasrl::detail::measure_fixed_slots(1, exact);
        require(boundary && *boundary == xasrl::detail::maximum_entry_size, "wire size boundary was rejected");
        const std::array<xasrl::szt, 1> overflow{maximum_payload + 1};
        require_error(xasrl::detail::measure_fixed_slots(1, overflow), xasrl::error_code::entry_too_large,
                      "container encoding accepted an oversized entry");
    }

    void test_malformed_input() {
        xasrl::byte_buffer pair_bytes{};
        const auto pair_bytes_status = xasrl::serialize(Pair{.first = 10, .second = 20}, pair_bytes, default_context);
        require(!pair_bytes_status, "Pair packing failed");

        const auto document = open_document(pair_bytes.span());
        const auto definition_rows = document.inline_definition_offset() + xasrl::detail::definition_header_size;
        auto oversized = pair_bytes;
        set_u64(oversized, document.root_offset(), document.entry.head().size + 1);
        Pair rejected_value_6{};
        require_error(xasrl::deserialize(oversized.span(), rejected_value_6, default_context),
                      xasrl::error_code::invalid_entry_size,
                      "oversized entry was accepted");

        auto trailing = pair_bytes;
        trailing.push_back(std::byte{});
        Pair rejected_value_7{};
        require_error(xasrl::deserialize(trailing.span(), rejected_value_7, default_context),
                      xasrl::error_code::trailing_bytes,
                      "trailing entry bytes were accepted");

        auto bad_reserved = pair_bytes;
        set_u32(bad_reserved, definition_rows + 4, 0);
        Pair rejected_value_8{};
        require_error(xasrl::deserialize(bad_reserved.span(), rejected_value_8, default_context),
                      xasrl::error_code::type_mismatch,
                      "invalid field wire type was accepted");

        auto bad_classification = pair_bytes;
        bad_classification[document.root_offset() + 11] = std::byte{};
        Pair rejected_value_9{};
        require_error(xasrl::deserialize(bad_classification.span(), rejected_value_9, default_context),
                      xasrl::error_code::invalid_entry_classification,
                      "invalid entry classification was accepted");

        auto duplicate = pair_bytes;
        set_u32(duplicate, definition_rows + xasrl::detail::definition_row_size, 1);
        Pair rejected_value_10{};
        require_error(xasrl::deserialize(duplicate.span(), rejected_value_10, default_context),
                      xasrl::error_code::invalid_mapping_order,
                      "duplicate field id was accepted");

        auto bad_address = pair_bytes;
        set_u64(bad_address, definition_rows + 8, bad_address.size());
        Pair rejected_value_11{};
        require_error(xasrl::deserialize(bad_address.span(), rejected_value_11, default_context),
                      xasrl::error_code::payload_out_of_bounds,
                      "out-of-bounds mapping was accepted");

        Pair rejected_value_12{};
        require_error(xasrl::deserialize(pair_bytes.span().first(pair_bytes.size() - 1), rejected_value_12, default_context),
                      xasrl::error_code::invalid_entry_size,
                      "truncated entry was accepted");

        xasrl::byte_buffer bool_bytes{};
        const auto bool_bytes_status = xasrl::serialize(true, bool_bytes, default_context);
        require(!bool_bytes_status, "bool packing failed");
        auto invalid_bool = bool_bytes;
        invalid_bool.span().back() = std::byte{2};
        bool rejected_value_13{};
        require_error(xasrl::deserialize(invalid_bool.span(), rejected_value_13, default_context),
                      xasrl::error_code::invalid_bool,
                      "invalid bool payload was accepted");

        xasrl::byte_buffer enum_bytes{};
        const auto enum_bytes_status = xasrl::serialize(activity::sleeping, enum_bytes, default_context);
        require(!enum_bytes_status, "enum packing failed");
        auto invalid_enum = enum_bytes;
        invalid_enum.span().back() = std::byte{7};
        activity rejected_value_14{};
        require_error(xasrl::deserialize(invalid_enum.span(), rejected_value_14, default_context),
                      xasrl::error_code::invalid_enum_value,
                      "invalid enum payload was accepted");

        xasrl::byte_buffer int_bytes{};
        const auto int_bytes_status = xasrl::serialize(42, int_bytes, default_context);
        require(!int_bytes_status, "integer packing failed");
        unsigned int rejected_value_15{};
        require_error(xasrl::deserialize(int_bytes.span(), rejected_value_15, default_context),
                      xasrl::error_code::type_mismatch,
                      "leaf root type mismatch was accepted");

        xasrl::byte_buffer map_bytes{};
        const auto map_bytes_status = xasrl::serialize(std::map<int, int>{{1, 2}, {3, 4}}, map_bytes, default_context);
        require(!map_bytes_status, "map packing failed");
        auto incomplete_pair = map_bytes;
        incomplete_pair[24] = std::byte{3};
        std::map<int, int> rejected_value_16{};
        require_error(xasrl::deserialize(incomplete_pair.span(), rejected_value_16, default_context),
                      xasrl::error_code::invalid_container_shape,
                      "incomplete key/value mapping group was accepted");

        auto wrong_container_id = map_bytes;
        set_u32(wrong_container_id, open_document(map_bytes.span()).inline_definition_offset() + 8, 4);
        std::map<int, int> rejected_value_17{};
        require_error(xasrl::deserialize(wrong_container_id.span(), rejected_value_17, default_context),
                      xasrl::error_code::invalid_mapping_order,
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
