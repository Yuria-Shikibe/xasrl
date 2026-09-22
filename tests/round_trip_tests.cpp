#include "test_support.hpp"

namespace srl_test {
    void test_record_round_trips() {
        const Person source{
            .age = 37,
            .score = 98.5,
            .runtime_cookie = 9001,
        };
        const auto packed = srl::pack(source, default_context);
        require(packed.has_value(), "Person packing failed");
        const auto packed_again = srl::pack(source, default_context);
        require(packed_again.has_value() && *packed == *packed_again,
                "packing must be deterministic");

        const auto extracted = srl::extract<Person>(packed->span(), default_context);
        require(extracted.has_value(), "Person extraction failed");
        require(extracted->age == source.age && extracted->score == source.score,
                "serialized Person fields changed");
        require(extracted->runtime_cookie == 41,
                "unmarked fields must retain defaults during extract");

        Person target{.runtime_cookie = 777};
        const auto unpacked = srl::unpack(packed->span(), target, default_context);
        require(unpacked.has_value(), "Person unpack failed");
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
        const auto profile_bytes = srl::pack(profile, default_context);
        require(profile_bytes.has_value(), "Profile packing failed");
        const auto profile_copy = srl::extract<Profile>(profile_bytes->span(), default_context);
        require(profile_copy.has_value(), "Profile extraction failed");
        require(profile_copy->person.age == source.age
                && profile_copy->person.score == source.score
                && profile_copy->name == profile.name
                && profile_copy->active
                && profile_copy->state == activity::running
                && profile_copy->position.x == profile.position.x
                && profile_copy->position.y == profile.position.y,
                "nested record or atomic field failed to round trip");
        require(profile_copy->person.runtime_cookie == 41
                && profile_copy->runtime_handle == nullptr,
                "nested unmarked state did not keep defaults");
    }

    void test_atomic_round_trips() {
        const Vec2 vector{.x = 1.5f, .y = -2.25f};
        const auto vector_bytes = srl::pack(vector, default_context);
        require(vector_bytes.has_value(), "Vec2 packing failed");
        require(vector_bytes->size()
                    == srl::detail::entry_head_wire_size
                       + 2 * srl::detail::entry_mapping_wire_size
                       + sizeof(float) * 2,
                "Vec2 auto_seq size is incorrect");
        require(get_u32(*vector_bytes, 4) == 2, "Vec2 auto_seq mapping count is incorrect");
        require(std::to_integer<std::uint8_t>((*vector_bytes)[9])
                    == std::to_underlying(srl::entry_kind::record)
                && std::to_integer<std::uint8_t>((*vector_bytes)[10])
                       == std::to_underlying(srl::mapping_kind::field)
                && std::to_integer<std::uint8_t>((*vector_bytes)[11]) == 1,
                "auto_seq entry classification is incorrect");
        require(get_u32(*vector_bytes, 16) == 0 && get_u32(*vector_bytes, 24) == 1,
                "auto_seq did not assign field ids from zero");
        const auto vector_copy = srl::extract<Vec2>(vector_bytes->span(), default_context);
        require(vector_copy.has_value()
                && vector_copy->x == vector.x
                && vector_copy->y == vector.y,
                "Vec2 atomic round trip failed");

        const Rect rectangle{
            .minimum = Vec2{.x = -1.0f, .y = -2.0f},
            .maximum = Vec2{.x = 4.0f, .y = 8.0f},
        };
        const auto rectangle_bytes = srl::pack(rectangle, default_context);
        require(rectangle_bytes.has_value(), "Rect packing failed");
        const auto rectangle_copy = srl::extract<Rect>(rectangle_bytes->span(), default_context);
        require(rectangle_copy.has_value()
                && rectangle_copy->minimum.x == rectangle.minimum.x
                && rectangle_copy->maximum.y == rectangle.maximum.y,
                "nested atomic members failed to round trip");

        const SmallText text{.value = "container-backed string"};
        const auto text_bytes = srl::pack(text, default_context);
        require(text_bytes.has_value(), "string container packing failed");
        const auto text_copy = srl::extract<SmallText>(text_bytes->span(), default_context);
        require(text_copy.has_value() && text_copy->value == text.value,
                "string container path failed to round trip");

        AutoSequenceRecord automatic{};
        automatic.base_value = 7;
        automatic.first = 11;
        automatic.runtime_only = 99;
        automatic.name = "auto sequence";
        automatic.active = true;
        const auto automatic_bytes = srl::pack(automatic, default_context);
        require(automatic_bytes.has_value(), "auto_seq packing failed");
        require(get_u32(*automatic_bytes, 4) == 4,
                "auto_seq transient member changed the field count");
        require(get_u32(*automatic_bytes, 16) == 0
                && get_u32(*automatic_bytes, 24) == 1
                && get_u32(*automatic_bytes, 32) == 4
                && get_u32(*automatic_bytes, 40) == 5,
                "auto_seq base/member ids or explicit id continuation are incorrect");
        const auto automatic_copy =
            srl::extract<AutoSequenceRecord>(automatic_bytes->span(), default_context);
        require(automatic_copy.has_value()
                && automatic_copy->base_value == automatic.base_value
                && automatic_copy->first == automatic.first
                && automatic_copy->runtime_only == 17
                && automatic_copy->name == automatic.name
                && automatic_copy->active == automatic.active,
                "auto_seq round trip failed");

        int base_pointee = 31;
        int member_pointee = 37;
        AutoSequenceEntityBypass entity_bypass{};
        entity_bypass.base_pointer = &base_pointee;
        entity_bypass.member_pointer = &member_pointee;
        entity_bypass.value = 41;
        const auto entity_bypass_bytes = srl::pack(entity_bypass, default_context);
        require(entity_bypass_bytes.has_value(), "entity-level auto_seq bypass packing failed");
        const auto entity_bypass_copy = srl::extract<AutoSequenceEntityBypass>(
            entity_bypass_bytes->span(), default_context);
        require(entity_bypass_copy.has_value()
                && entity_bypass_copy->base_pointer == &base_pointee
                && entity_bypass_copy->member_pointer == &member_pointee
                && entity_bypass_copy->value == entity_bypass.value,
                "entity-level auto_seq bypass failed to preserve pointer representations");

        AutoSequenceTypeBypass type_bypass{};
        type_bypass.pointer = &member_pointee;
        type_bypass.value = 43;
        const auto type_bypass_bytes = srl::pack(type_bypass, default_context);
        require(type_bypass_bytes.has_value(), "type-level auto_seq bypass packing failed");
        const auto type_bypass_copy = srl::extract<AutoSequenceTypeBypass>(
            type_bypass_bytes->span(), default_context);
        require(type_bypass_copy.has_value()
                && type_bypass_copy->pointer == &member_pointee
                && type_bypass_copy->value == type_bypass.value,
                "type-level auto_seq bypass failed to preserve pointer representation");

        const PaddedAtomic padded{.small = 7, .large = 0x11223344u};
        const auto padded_bytes = srl::pack(padded, default_context);
        require(padded_bytes.has_value()
                && padded_bytes->size() == srl::detail::entry_head_wire_size + sizeof(PaddedAtomic),
                "padding-enabled atomic size is incorrect");
        require(std::memcmp(
                    padded_bytes->data() + srl::detail::entry_head_wire_size,
                    &padded,
                    sizeof(PaddedAtomic)) == 0,
                "padding-enabled atomic did not copy the complete object representation");
        const auto padded_copy = srl::extract<PaddedAtomic>(padded_bytes->span(), default_context);
        require(padded_copy.has_value()
                && padded_copy->small == padded.small
                && padded_copy->large == padded.large,
                "padding-enabled atomic failed to round trip");

        const UniqueAtomic unique{.first = 11, .second = 22};
        const auto unique_bytes = srl::pack(unique, default_context);
        require(unique_bytes.has_value(), "unique-representation atomic packing failed");
        const auto unique_copy = srl::extract<UniqueAtomic>(unique_bytes->span(), default_context);
        require(unique_copy.has_value()
                && unique_copy->first == unique.first
                && unique_copy->second == unique.second,
                "unique-representation atomic failed to round trip");

        const TransientAtomic transient{
            .first = 0x11223344u,
            .second = 0x55667788u,
            .runtime_only = 99,
            .third = 0x99aabbccu,
            .fourth = 0xddeeff00u,
        };
        const auto transient_bytes = srl::pack(transient, default_context);
        require(transient_bytes.has_value()
                && transient_bytes->size()
                       == srl::detail::entry_head_wire_size + sizeof(std::uint32_t) * 4,
                "transient atomic size is incorrect");
        const auto transient_copy =
            srl::extract<TransientAtomic>(transient_bytes->span(), default_context);
        require(transient_copy.has_value()
                && transient_copy->first == transient.first
                && transient_copy->second == transient.second
                && transient_copy->runtime_only == 17
                && transient_copy->third == transient.third
                && transient_copy->fourth == transient.fourth,
                "transient atomic member was serialized or lost its default");

        int transient_pointee = 42;
        const TransientPointerAtomic transient_pointer{
            .runtime_pointer = &transient_pointee,
            .value = 0x12345678u,
        };
        const auto transient_pointer_bytes = srl::pack(transient_pointer, default_context);
        require(transient_pointer_bytes.has_value(),
                "transient pointer atomic packing failed");
        const auto transient_pointer_copy = srl::extract<TransientPointerAtomic>(
            transient_pointer_bytes->span(), default_context);
        require(transient_pointer_copy.has_value()
                && transient_pointer_copy->runtime_pointer == nullptr
                && transient_pointer_copy->value == transient_pointer.value,
                "transient pointer leaked into serialized object representation");

        int pointee = 42;
        const BypassedAtomicPointer bypassed{.pointer = &pointee};
        const auto bypassed_bytes = srl::pack(bypassed, default_context);
        require(bypassed_bytes.has_value(), "bypassed pointer atomic packing failed");
        const auto bypassed_copy = srl::extract<BypassedAtomicPointer>(
            bypassed_bytes->span(), default_context);
        require(bypassed_copy.has_value() && bypassed_copy->pointer == &pointee,
                "bypassed pointer atomic failed to preserve its representation");

        const AtomicEntityBypass atomic_entity_bypass{.pointer = &pointee};
        const auto atomic_entity_bytes = srl::pack(atomic_entity_bypass, default_context);
        require(atomic_entity_bytes.has_value(), "entity-level atomic bypass packing failed");
        const auto atomic_entity_copy = srl::extract<AtomicEntityBypass>(
            atomic_entity_bytes->span(), default_context);
        require(atomic_entity_copy.has_value() && atomic_entity_copy->pointer == &pointee,
                "entity-level atomic bypass failed to preserve pointer representation");

        AtomicBaseBypass atomic_base_bypass{};
        atomic_base_bypass.pointer = &pointee;
        const auto atomic_base_bytes = srl::pack(atomic_base_bypass, default_context);
        require(atomic_base_bytes.has_value(), "base-level atomic bypass packing failed");
        const auto atomic_base_copy = srl::extract<AtomicBaseBypass>(
            atomic_base_bytes->span(), default_context);
        require(atomic_base_copy.has_value() && atomic_base_copy->pointer == &pointee,
                "base-level atomic bypass failed to preserve pointer representation");

        const ForcedAtomicField forced{
            .value = ThirdPartyTrivial{.first = 0x11223344u, .second = 0x55667788u},
        };
        const auto forced_bytes = srl::pack(forced, default_context);
        require(forced_bytes.has_value(), "field-level forced atomic packing failed");
        const auto forced_copy = srl::extract<ForcedAtomicField>(forced_bytes->span(), default_context);
        require(forced_copy.has_value() && forced_copy->value == forced.value,
                "field-level forced atomic failed to round trip");
    }


TEST(SerializationRoundTrip, Records) {
    test_record_round_trips();
}

TEST(SerializationRoundTrip, AtomicValues) {
    test_atomic_round_trips();
}
}
