#include "test_support.hpp"

namespace srl_test {
    void test_record_round_trips() {
        const Person source{
            .age = 37,
            .score = 98.5,
            .runtime_cookie = 9001,
        };
        srl::byte_buffer packed{};
        const auto packed_status = srl::serialize(source, packed, default_context);
        require(!packed_status, "Person packing failed");
        srl::byte_buffer packed_again{};
        const auto packed_again_status = srl::serialize(source, packed_again, default_context);
        require(!packed_again_status && packed == packed_again,
                "packing must be deterministic");

        Person extracted{};
        const auto extracted_status = srl::deserialize(packed.span(), extracted, default_context);
        require(!extracted_status, "Person extraction failed");
        require(extracted.age == source.age && extracted.score == source.score,
                "serialized Person fields changed");
        require(extracted.runtime_cookie == 41,
                "unmarked fields must retain defaults during extract");

        Person target{.runtime_cookie = 777};
        const auto unpacked = srl::deserialize(packed.span(), target, default_context);
        require(!unpacked, "Person unpack failed");
        require(target.age == source.age && target.score == source.score,
                "Person unpack produced incorrect fields");
        require(target.runtime_cookie == 777,
                "unpack must preserve existing unmarked fields");

        const Profile profile{
            .person = source,
            .name = "reflection profile",
            .active = true,
            .state = activity::running,
            .position = Vec2{.x = 12.5f, .y = -3.25f},
            .runtime_handle = reinterpret_cast<void *>(0x1234),
        };
        srl::byte_buffer profile_bytes{};
        const auto profile_bytes_status = srl::serialize(profile, profile_bytes, default_context);
        require(!profile_bytes_status, "Profile packing failed");
        Profile profile_copy{};
        const auto profile_copy_status = srl::deserialize(profile_bytes.span(), profile_copy, default_context);
        require(!profile_copy_status, "Profile extraction failed");
        require(profile_copy.person.age == source.age
                && profile_copy.person.score == source.score
                && profile_copy.name == profile.name
                && profile_copy.active
                && profile_copy.state == activity::running
                && profile_copy.position.x == profile.position.x
                && profile_copy.position.y == profile.position.y,
                "nested record or leaf field failed to round trip");
        require(profile_copy.person.runtime_cookie == 41
                && profile_copy.runtime_handle == nullptr,
                "nested unmarked state did not keep defaults");
    }

    void test_leaf_round_trips() {
        const Vec2 vector{.x = 1.5f, .y = -2.25f};
        srl::byte_buffer vector_bytes{};
        const auto vector_bytes_status = srl::serialize(vector, vector_bytes, default_context);
        require(!vector_bytes_status, "Vec2 packing failed");
        const auto vector_doc = open_document(vector_bytes.span());
        require(vector_bytes.size() == srl::serialized_size<Vec2>() && vector_doc.entry.head().mapping_count() == 2,
                "Vec2 auto_seq size or field count is incorrect");
        require(vector_doc.entry.head().kind() == srl::entry_kind::record
                && vector_doc.at(0).id == 0 && vector_doc.at(1).id == 1,
                "auto_seq entry classification or IDs are incorrect");
        Vec2 vector_copy{};
        const auto vector_copy_status = srl::deserialize(vector_bytes.span(), vector_copy, default_context);
        require(!vector_copy_status
                && vector_copy.x == vector.x
                && vector_copy.y == vector.y,
                "Vec2 leaf round trip failed");

        const Rect rectangle{
            .minimum = Vec2{.x = -1.0f, .y = -2.0f},
            .maximum = Vec2{.x = 4.0f, .y = 8.0f},
        };
        srl::byte_buffer rectangle_bytes{};
        const auto rectangle_bytes_status = srl::serialize(rectangle, rectangle_bytes, default_context);
        require(!rectangle_bytes_status, "Rect packing failed");
        Rect rectangle_copy{};
        const auto rectangle_copy_status = srl::deserialize(rectangle_bytes.span(), rectangle_copy, default_context);
        require(!rectangle_copy_status
                && rectangle_copy.minimum.x == rectangle.minimum.x
                && rectangle_copy.maximum.y == rectangle.maximum.y,
                "nested leaf members failed to round trip");

        const SmallText text{.value = "container-backed string"};
        srl::byte_buffer text_bytes{};
        const auto text_bytes_status = srl::serialize(text, text_bytes, default_context);
        require(!text_bytes_status, "string container packing failed");
        SmallText text_copy{};
        const auto text_copy_status = srl::deserialize(text_bytes.span(), text_copy, default_context);
        require(!text_copy_status && text_copy.value == text.value,
                "string container path failed to round trip");

        AutoSequenceRecord automatic{};
        automatic.base_value = 7;
        automatic.first = 11;
        automatic.runtime_only = 99;
        automatic.name = "auto sequence";
        automatic.active = true;
        srl::byte_buffer automatic_bytes{};
        const auto automatic_bytes_status = srl::serialize(automatic, automatic_bytes, default_context);
        require(!automatic_bytes_status, "auto_seq packing failed");
        const auto automatic_doc = open_document(automatic_bytes.span());
        require(automatic_doc.entry.head().mapping_count() == 4,
                "auto_seq transient member changed the field count");
        require(automatic_doc.at(0).id == 0 && automatic_doc.at(1).id == 1
                && automatic_doc.at(2).id == 4 && automatic_doc.at(3).id == 5,
                "auto_seq base/member ids or explicit id continuation are incorrect");
        AutoSequenceRecord automatic_copy{};
        const auto automatic_copy_status = srl::deserialize(automatic_bytes.span(), automatic_copy, default_context);
        require(!automatic_copy_status
                && automatic_copy.base_value == automatic.base_value
                && automatic_copy.first == automatic.first
                && automatic_copy.runtime_only == 17
                && automatic_copy.name == automatic.name
                && automatic_copy.active == automatic.active,
                "auto_seq round trip failed");

        int base_pointee = 31;
        int member_pointee = 37;
        AutoSequenceEntityBypass entity_bypass{};
        entity_bypass.base_pointer = &base_pointee;
        entity_bypass.member_pointer = &member_pointee;
        entity_bypass.value = 41;
        srl::byte_buffer entity_bypass_bytes{};
        const auto entity_bypass_bytes_status = srl::serialize(entity_bypass, entity_bypass_bytes, default_context);
        require(!entity_bypass_bytes_status, "entity-level auto_seq bypass packing failed");
        AutoSequenceEntityBypass entity_bypass_copy{};
        const auto entity_bypass_copy_status = srl::deserialize(entity_bypass_bytes.span(), entity_bypass_copy, default_context);
        require(!entity_bypass_copy_status
                && entity_bypass_copy.base_pointer == &base_pointee
                && entity_bypass_copy.member_pointer == &member_pointee
                && entity_bypass_copy.value == entity_bypass.value,
                "entity-level auto_seq bypass failed to preserve pointer representations");

        AutoSequenceTypeBypass type_bypass{};
        type_bypass.pointer = &member_pointee;
        type_bypass.value = 43;
        srl::byte_buffer type_bypass_bytes{};
        const auto type_bypass_bytes_status = srl::serialize(type_bypass, type_bypass_bytes, default_context);
        require(!type_bypass_bytes_status, "type-level auto_seq bypass packing failed");
        AutoSequenceTypeBypass type_bypass_copy{};
        const auto type_bypass_copy_status = srl::deserialize(type_bypass_bytes.span(), type_bypass_copy, default_context);
        require(!type_bypass_copy_status
                && type_bypass_copy.pointer == &member_pointee
                && type_bypass_copy.value == type_bypass.value,
                "type-level auto_seq bypass failed to preserve pointer representation");

        const PaddedLeaf padded{.small = 7, .large = 0x11223344u};
        srl::byte_buffer padded_bytes{};
        const auto padded_bytes_status = srl::serialize(padded, padded_bytes, default_context);
        require(!padded_bytes_status
                && padded_bytes.size() == (srl::detail::document_header_size + srl::detail::entry_head_wire_size) + sizeof(PaddedLeaf),
                "padding-enabled leaf size is incorrect");
        const auto padded_payload = padded_bytes.span().subspan((srl::detail::document_header_size + srl::detail::entry_head_wire_size));
        require(padded_payload[0] == std::byte{7}
                && padded_payload[1] == std::byte{0}
                && padded_payload[2] == std::byte{0}
                && padded_payload[3] == std::byte{0}
                && srl::detail::read_trivial<std::uint32_t>(padded_payload.subspan(4)) == 0x11223344u,
                "padding-enabled leaf must emit canonical zero padding");
        PaddedLeaf padded_copy{};
        const auto padded_copy_status = srl::deserialize(padded_bytes.span(), padded_copy, default_context);
        require(!padded_copy_status
                && padded_copy.small == padded.small
                && padded_copy.large == padded.large,
                "padding-enabled leaf failed to round trip");

        const UniqueLeaf unique{.first = 11, .second = 22};
        srl::byte_buffer unique_bytes{};
        const auto unique_bytes_status = srl::serialize(unique, unique_bytes, default_context);
        require(!unique_bytes_status, "unique-representation leaf packing failed");
        UniqueLeaf unique_copy{};
        const auto unique_copy_status = srl::deserialize(unique_bytes.span(), unique_copy, default_context);
        require(!unique_copy_status
                && unique_copy.first == unique.first
                && unique_copy.second == unique.second,
                "unique-representation leaf failed to round trip");

        const TransientLeaf transient{
            .first = 0x11223344u,
            .second = 0x55667788u,
            .runtime_only = 99,
            .third = 0x99aabbccu,
            .fourth = 0xddeeff00u,
        };
        srl::byte_buffer transient_bytes{};
        const auto transient_bytes_status = srl::serialize(transient, transient_bytes, default_context);
        require(!transient_bytes_status
                && transient_bytes.size()
                       == (srl::detail::document_header_size + srl::detail::entry_head_wire_size) + sizeof(std::uint32_t) * 4,
                "transient leaf size is incorrect");
        TransientLeaf transient_copy{};
        const auto transient_copy_status = srl::deserialize(transient_bytes.span(), transient_copy, default_context);
        require(!transient_copy_status
                && transient_copy.first == transient.first
                && transient_copy.second == transient.second
                && transient_copy.runtime_only == 17
                && transient_copy.third == transient.third
                && transient_copy.fourth == transient.fourth,
                "transient leaf member was serialized or lost its default");

        int transient_pointee = 42;
        const TransientPointerLeaf transient_pointer{
            .runtime_pointer = &transient_pointee,
            .value = 0x12345678u,
        };
        srl::byte_buffer transient_pointer_bytes{};
        const auto transient_pointer_bytes_status = srl::serialize(transient_pointer, transient_pointer_bytes, default_context);
        require(!transient_pointer_bytes_status,
                "transient pointer leaf packing failed");
        TransientPointerLeaf transient_pointer_copy{};
        const auto transient_pointer_copy_status = srl::deserialize(transient_pointer_bytes.span(), transient_pointer_copy, default_context);
        require(!transient_pointer_copy_status
                && transient_pointer_copy.runtime_pointer == nullptr
                && transient_pointer_copy.value == transient_pointer.value,
                "transient pointer leaked into serialized object representation");

        int pointee = 42;
        const BypassedLeafPointer bypassed{.pointer = &pointee};
        srl::byte_buffer bypassed_bytes{};
        const auto bypassed_bytes_status = srl::serialize(bypassed, bypassed_bytes, default_context);
        require(!bypassed_bytes_status, "bypassed pointer leaf packing failed");
        BypassedLeafPointer bypassed_copy{};
        const auto bypassed_copy_status = srl::deserialize(bypassed_bytes.span(), bypassed_copy, default_context);
        require(!bypassed_copy_status && bypassed_copy.pointer == &pointee,
                "bypassed pointer leaf failed to preserve its representation");

        const LeafEntityBypass leaf_entity_bypass{.pointer = &pointee};
        srl::byte_buffer leaf_entity_bytes{};
        const auto leaf_entity_bytes_status = srl::serialize(leaf_entity_bypass, leaf_entity_bytes, default_context);
        require(!leaf_entity_bytes_status, "entity-level leaf bypass packing failed");
        LeafEntityBypass leaf_entity_copy{};
        const auto leaf_entity_copy_status = srl::deserialize(leaf_entity_bytes.span(), leaf_entity_copy, default_context);
        require(!leaf_entity_copy_status && leaf_entity_copy.pointer == &pointee,
                "entity-level leaf bypass failed to preserve pointer representation");

        LeafBaseBypass leaf_base_bypass{};
        leaf_base_bypass.pointer = &pointee;
        srl::byte_buffer leaf_base_bytes{};
        const auto leaf_base_bytes_status = srl::serialize(leaf_base_bypass, leaf_base_bytes, default_context);
        require(!leaf_base_bytes_status, "base-level leaf bypass packing failed");
        LeafBaseBypass leaf_base_copy{};
        const auto leaf_base_copy_status = srl::deserialize(leaf_base_bytes.span(), leaf_base_copy, default_context);
        require(!leaf_base_copy_status && leaf_base_copy.pointer == &pointee,
                "base-level leaf bypass failed to preserve pointer representation");

        const ForcedLeafField forced{
            .value = ThirdPartyTrivial{.first = 0x11223344u, .second = 0x55667788u},
        };
        srl::byte_buffer forced_bytes{};
        const auto forced_bytes_status = srl::serialize(forced, forced_bytes, default_context);
        require(!forced_bytes_status, "field-level forced leaf packing failed");
        ForcedLeafField forced_copy{};
        const auto forced_copy_status = srl::deserialize(forced_bytes.span(), forced_copy, default_context);
        require(!forced_copy_status && forced_copy.value == forced.value,
                "field-level forced leaf failed to round trip");
    }


TEST(SerializationRoundTrip, Records) {
    test_record_round_trips();
}

TEST(SerializationRoundTrip, LeafValues) {
    test_leaf_round_trips();
}
}
