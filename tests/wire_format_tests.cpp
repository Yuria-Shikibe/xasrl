#include "test_support.hpp"

namespace srl_test {
    void test_mapping_layout() {
        const auto bytes = srl::pack(Pair{
            .first = 0x11223344,
            .second = 0x55667788,
        }, default_context);
        require(bytes.has_value(), "Pair packing failed");
        require(bytes->size() == 40, "Pair entry size changed");
        require(get_u32(*bytes, 0) == 40, "entry head size is incorrect");
        require(get_u32(*bytes, 4) == 2, "mapping count is incorrect");
        require(std::to_integer<std::uint8_t>((*bytes)[8]) == 1,
                "initial mapping alignment must be one byte");
        require(std::to_integer<std::uint8_t>((*bytes)[9])
                    == std::to_underlying(srl::entry_kind::record)
                && std::to_integer<std::uint8_t>((*bytes)[10])
                       == std::to_underlying(srl::mapping_kind::field)
                && std::to_integer<std::uint8_t>((*bytes)[11]) == 1,
                "record entry classification is incorrect");
        require(get_u32(*bytes, 12) == srl::type_hash<Pair>(),
                "entry type hash is incorrect");
        require(get_u32(*bytes, 16) == 1 && get_u32(*bytes, 20) == 0,
                "first mapping is incorrect");
        require(get_u32(*bytes, 24) == 2 && get_u32(*bytes, 28) == 4,
                "second mapping is incorrect");
        require(get_u32(*bytes, 32) == 0x11223344u
                && get_u32(*bytes, 36) == 0x55667788u,
                "content is not canonical little endian");

        auto entry = srl::detail::parse_entry(bytes->span());
        require(entry.has_value() && entry->head.mapping_count == 2,
                "Pair mapping table did not parse");

        const auto direct_size = srl::serialized_size(Pair{.first = 0x11223344, .second = 0x55667788}, default_context);
        require(direct_size.has_value() && *direct_size == bytes->size(),
                "serialized_size did not match pack output");
        srl::byte_buffer direct;
        direct.resize(*direct_size);
        require(!srl::pack_into(
                    direct.span(),
                    Pair{.first = 0x11223344, .second = 0x55667788}, default_context)
                && direct == *bytes,
                "pack_into did not write directly into the supplied span");
    }

    void test_compatible_views() {
        const auto full = srl::pack(Pair{.first = 10, .second = 20}, default_context);
        require(full.has_value(), "full view packing failed");
        const auto subset = srl::extract<FirstOnly>(full->span(), default_context);
        require(subset.has_value() && subset->first == 10,
                "unknown stored fields must be ignored");

        const auto partial = srl::pack(FirstOnly{.first = 10}, default_context);
        require(partial.has_value(), "partial view packing failed");
        require_error(srl::extract<Pair>(partial->span(), default_context),
                      srl::error_code::missing_field,
                      "missing required field was accepted");
    }

    void test_container_round_trips() {
        const std::vector<int> vector{1, 2, 3, 5, 8};
        const auto vector_bytes = srl::pack(vector, default_context);
        require(vector_bytes.has_value(), "vector packing failed");
        require(std::to_integer<std::uint8_t>((*vector_bytes)[9])
                    == std::to_underlying(srl::entry_kind::container)
                && std::to_integer<std::uint8_t>((*vector_bytes)[10])
                       == std::to_underlying(srl::mapping_kind::element)
                && std::to_integer<std::uint8_t>((*vector_bytes)[11]) == 1,
                "single-value container classification is incorrect");
        const auto vector_copy = srl::extract<std::vector<int>>(vector_bytes->span(), default_context);
        require(vector_copy.has_value() && *vector_copy == vector,
                "vector round trip failed");

        const auto list_copy = srl::extract<std::list<int>>(vector_bytes->span(), default_context);
        require(list_copy.has_value() && std::ranges::equal(*list_copy, vector),
                "compatible vector-to-list decode failed");

        const std::forward_list<int> forward{4, 3, 2, 1};
        const auto forward_bytes = srl::pack(forward, default_context);
        require(forward_bytes.has_value(), "forward_list packing failed");
        const auto forward_copy = srl::extract<std::forward_list<int>>(forward_bytes->span(), default_context);
        require(forward_copy.has_value() && *forward_copy == forward,
                "forward_list order changed");

        const std::array<int, 3> array{9, 8, 7};
        const auto array_bytes = srl::pack(array, default_context);
        require(array_bytes.has_value(), "array packing failed");
        const auto array_copy = srl::extract<std::array<int, 3>>(array_bytes->span(), default_context);
        require(array_copy.has_value() && *array_copy == array,
                "array round trip failed");
        require_error(srl::extract<std::array<int, 2>>(array_bytes->span(), default_context),
                      srl::error_code::fixed_container_size_mismatch,
                      "array element-count mismatch was accepted");

        const FixedSequence<3> fixed{{7, 5, 3}};
        const auto fixed_bytes = srl::pack(fixed, default_context);
        require(fixed_bytes.has_value(), "automatically detected fixed container packing failed");
        const auto fixed_copy = srl::extract<FixedSequence<3>>(fixed_bytes->span(), default_context);
        require(fixed_copy.has_value() && *fixed_copy == fixed,
                "automatically detected fixed container round trip failed");
        require_error(srl::extract<FixedSequence<2>>(fixed_bytes->span(), default_context),
                      srl::error_code::fixed_container_size_mismatch,
                      "automatically detected fixed container mismatch was accepted");

        int native_array[3]{6, 4, 2};
        const auto native_array_bytes = srl::pack(native_array, default_context);
        require(native_array_bytes.has_value(), "native array packing failed");
        int native_array_copy[3]{};
                require(!srl::unpack(native_array_bytes->span(), native_array_copy, default_context)
                && std::ranges::equal(native_array, native_array_copy),
                "native array round trip failed");

        const RawArrayField raw_field{.values = {3, 1, 4}};
        const auto raw_field_bytes = srl::pack(raw_field, default_context);
        require(raw_field_bytes.has_value(), "native array field packing failed");
        const auto raw_field_copy = srl::extract<RawArrayField>(raw_field_bytes->span(), default_context);
        require(raw_field_copy.has_value()
                && std::ranges::equal(raw_field.values, raw_field_copy->values),
                "native array field round trip failed");

        const std::vector<std::vector<int>> nested{{1, 2}, {}, {3, 4, 5}};
        const auto nested_bytes = srl::pack(nested, default_context);
        require(nested_bytes.has_value(), "nested container packing failed");
        const auto nested_copy =
            srl::extract<std::vector<std::vector<int>>>(nested_bytes->span(), default_context);
        require(nested_copy.has_value() && *nested_copy == nested,
                "nested container round trip failed");

        const std::vector<bool> bits{true, false, true, true};
        const auto bit_bytes = srl::pack(bits, default_context);
        require(bit_bytes.has_value(), "vector<bool> packing failed");
        const auto bit_copy = srl::extract<std::vector<bool>>(bit_bytes->span(), default_context);
        require(bit_copy.has_value() && *bit_copy == bits,
                "vector<bool> proxy elements failed to round trip");

        TrackingSequence tracking{.values = {6, 7, 8}};
        const auto tracking_bytes = srl::pack(tracking, default_context);
        require(tracking_bytes.has_value(), "custom container packing failed");
        const auto tracking_copy = srl::extract<TrackingSequence>(tracking_bytes->span(), default_context);
        require(tracking_copy.has_value()
                && tracking_copy->values == tracking.values
                && tracking_copy->reserved == tracking.values.size(),
                "custom traits or reserve optimization failed");

        const std::map<int, std::string> map{{1, "one"}, {2, "two"}};
        const auto map_bytes = srl::pack(map, default_context);
        require(map_bytes.has_value(), "map packing failed");
        require(get_u32(*map_bytes, 4) == map.size() * 2
                && std::to_integer<std::uint8_t>((*map_bytes)[11]) == 2,
                "map key/value slots were not grouped in pairs");
        const auto unordered_copy = srl::extract<std::unordered_map<int, std::string>>(
            map_bytes->span(), default_context);
        require(unordered_copy.has_value()
                && unordered_copy->at(1) == "one"
                && unordered_copy->at(2) == "two",
                "map-to-unordered_map decode failed");

        const std::vector<int> duplicates{3, 3, 5};
        const auto duplicate_bytes = srl::pack(duplicates, default_context);
        require(duplicate_bytes.has_value(), "duplicate sequence packing failed");
        const auto deduplicated = srl::extract<std::set<int>>(duplicate_bytes->span(), default_context);
        require(deduplicated.has_value() && *deduplicated == std::set<int>{3, 5},
                "default unique-container deduplication failed");
        require_error(
            srl::extract<std::set<int>>(
                duplicate_bytes->span(),
                rejecting_context),
            srl::error_code::duplicate_container_element,
            "root duplicate rejection was not enforced");

        const auto field_bytes = srl::pack(VectorField{.values = duplicates}, default_context);
        require(field_bytes.has_value(), "container field packing failed");
        require_error(srl::extract<RejectingSetField>(field_bytes->span(), default_context),
                      srl::error_code::duplicate_container_element,
                      "field container duplicate tag was not enforced");
    }

    void test_container_entry_size_limit() {
        using codec = srl::detail::root_codec<std::vector<int>>;

        codec::encode_plan plan;
        plan.payload.size = std::numeric_limits<std::uint32_t>::max()
            - srl::detail::entry_mapping_wire_size;

        const auto status = plan.prepare(std::vector<int>{1}, default_context);
        require(status.code == srl::error_code::entry_too_large,
                "container encoding accepted an entry too large for the wire size field");
    }

    void test_malformed_input() {
        const auto pair_bytes = srl::pack(Pair{.first = 10, .second = 20}, default_context);
        require(pair_bytes.has_value(), "Pair packing failed");

        auto oversized = *pair_bytes;
        set_u32(oversized, 0, static_cast<std::uint32_t>(oversized.size() + 1));
        require_error(srl::extract<Pair>(oversized.span(), default_context),
                      srl::error_code::invalid_entry_size,
                      "oversized entry was accepted");

        auto trailing = *pair_bytes;
        trailing.push_back(std::byte{});
        require_error(srl::extract<Pair>(trailing.span(), default_context),
                      srl::error_code::trailing_bytes,
                      "trailing entry bytes were accepted");

        auto bad_alignment = *pair_bytes;
        bad_alignment[8] = std::byte{2};
        require_error(srl::extract<Pair>(bad_alignment.span(), default_context),
                      srl::error_code::unsupported_mapping_alignment,
                      "unsupported mapping alignment was accepted");

        auto bad_classification = *pair_bytes;
        bad_classification[9] = std::byte{};
        require_error(srl::extract<Pair>(bad_classification.span(), default_context),
                      srl::error_code::invalid_entry_classification,
                      "invalid entry classification was accepted");

        auto duplicate = *pair_bytes;
        set_u32(duplicate, 24, 1);
        require_error(srl::extract<Pair>(duplicate.span(), default_context),
                      srl::error_code::invalid_mapping_order,
                      "duplicate field id was accepted");

        auto bad_address = *pair_bytes;
        set_u32(bad_address, 20, std::numeric_limits<std::uint32_t>::max());
        require_error(srl::extract<Pair>(bad_address.span(), default_context),
                      srl::error_code::payload_out_of_bounds,
                      "out-of-bounds mapping was accepted");

        require_error(srl::extract<Pair>(pair_bytes->span().first(pair_bytes->size() - 1), default_context),
                      srl::error_code::invalid_entry_size,
                      "truncated entry was accepted");

        const auto bool_bytes = srl::pack(true, default_context);
        require(bool_bytes.has_value(), "bool packing failed");
        auto invalid_bool = *bool_bytes;
        invalid_bool[srl::detail::entry_head_wire_size] = std::byte{2};
        require_error(srl::extract<bool>(invalid_bool.span(), default_context),
                      srl::error_code::invalid_bool,
                      "invalid bool payload was accepted");

        const auto enum_bytes = srl::pack(activity::sleeping, default_context);
        require(enum_bytes.has_value(), "enum packing failed");
        auto invalid_enum = *enum_bytes;
        invalid_enum[srl::detail::entry_head_wire_size] = std::byte{7};
        require_error(srl::extract<activity>(invalid_enum.span(), default_context),
                      srl::error_code::invalid_enum_value,
                      "invalid enum payload was accepted");

        const auto int_bytes = srl::pack(42, default_context);
        require(int_bytes.has_value(), "integer packing failed");
        require_error(srl::extract<unsigned int>(int_bytes->span(), default_context),
                      srl::error_code::type_mismatch,
                      "atomic root type mismatch was accepted");

        const auto map_bytes = srl::pack(std::map<int, int>{{1, 2}, {3, 4}}, default_context);
        require(map_bytes.has_value(), "map packing failed");
        auto incomplete_pair = *map_bytes;
        set_u32(incomplete_pair, 4, 3);
        require_error(srl::extract<std::map<int, int>>(incomplete_pair.span(), default_context),
                      srl::error_code::invalid_container_shape,
                      "incomplete key/value mapping group was accepted");

        auto wrong_container_id = *map_bytes;
        set_u32(wrong_container_id, 16, 4);
        require_error(srl::extract<std::map<int, int>>(wrong_container_id.span(), default_context),
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
