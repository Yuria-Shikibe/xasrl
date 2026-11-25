#include "srl/error.hpp"

#include <gtest/gtest.h>
#include <concepts>
#include <limits>
#include <map>
#include <vector>

#include "test_models.hpp"

static_assert(srl::schema_valid(^^Person));
static_assert(srl::schema_valid(^^Vec2));
static_assert(srl::schema_valid(^^Rect));
static_assert(srl::schema_valid(^^PaddedLeaf));
static_assert(srl::schema_valid(^^UniqueLeaf));
static_assert(!srl::schema_valid(^^NonTrivialMemberwiseLeaf));
static_assert(srl::schema_valid(^^TransientLeaf));
static_assert(srl::schema_valid(^^TransientPointerLeaf));
static_assert(!srl::schema_valid(^^InvalidRecordTransient));
static_assert(!srl::schema_valid(^^DerivedLeaf));
static_assert(!srl::schema_valid(^^PrivateDerivedLeaf));
static_assert(!srl::schema_valid(^^ProtectedDerivedLeaf));
static_assert(!srl::schema_valid(^^VirtualDerivedLeaf));
static_assert(!srl::schema_valid(^^NonTrivialPaddedLeaf));
static_assert(srl::schema_valid(^^SmallText));
static_assert(srl::schema_valid(^^AutoSequenceBase));
static_assert(srl::schema_valid(^^AutoSequenceRecord));
static_assert(!srl::schema_valid(^^AutoSequenceDuplicate));
static_assert(!srl::schema_valid(^^AutoSequenceOverflow));
static_assert(srl::schema_valid(^^AutoSequenceEntityBypass));
static_assert(srl::schema_valid(^^AutoSequenceTypeBypass));
static_assert(!srl::schema_valid(^^AutoSequenceRejectedPointer));
static_assert(srl::schema_valid(^^LeafEntityBypass));
static_assert(srl::schema_valid(^^LeafBaseBypass));
static_assert(!srl::leaf_serializable<RuntimeFixedSize>);
static_assert(!srl::schema_valid(^^RuntimeFixedSize));
static_assert(!srl::leaf_serializable<DynamicSpanCodec>);
static_assert(!srl::schema_valid(^^DynamicSpanCodec));
static_assert(srl::schema_valid(^^Profile));
static_assert(srl::schema_valid(^^Pair));
static_assert(srl::schema_valid(^^FirstOnly));
static_assert(!srl::schema_valid(^^DuplicateIds));
static_assert(srl::inspect_schema(^^DuplicateIds).reason == srl::schema_error::duplicate_field_id);
static_assert(srl::schema_diagnostic(^^DuplicateIds).contains("duplicate_field_id"));
static_assert(srl::inspect_schema(^^MarkedPointer).reason == srl::schema_error::unsupported_entity);
static_assert(!srl::schema_valid(^^MarkedPointer));
static_assert(!srl::schema_valid(^^LeafPointer));
static_assert(srl::schema_valid(^^BypassedLeafPointer));
static_assert(!srl::schema_valid(^^UnsupportedVariableLeaf));
static_assert(!srl::schema_valid(^^AmbiguousLeaf));
static_assert(srl::schema_valid(^^int[3]));
static_assert(!srl::schema_valid(^^int *[3]));
static_assert(srl::schema_valid(^^std::array<int, 3>));
static_assert(srl::container_traits<std::array<int, 3>>::fixed_size == 3);
static_assert(srl::container_traits<int[3]>::fixed_size == 3);
static_assert(srl::container_traits<FixedSequence<4>>::fixed_size == 4);
static_assert(srl::container_traits<std::vector<int>>::fixed_size == std::dynamic_extent);
static_assert(srl::container_traits<std::span<int, 4>>::fixed_size == 4);
static_assert(!srl::container_serializable<std::span<int, 4>>);
static_assert(srl::schema_valid(^^FixedSequence<4>));
static_assert(srl::schema_valid(^^std::vector<int>));
static_assert(srl::schema_valid(^^std::deque<std::string>));
static_assert(srl::schema_valid(^^std::list<Person>));
static_assert(srl::schema_valid(^^std::forward_list<int>));
static_assert(srl::schema_valid(^^std::set<int>));
static_assert(srl::schema_valid(^^std::multiset<int>));
static_assert(srl::schema_valid(^^std::map<int, std::string>));
static_assert(srl::schema_valid(^^std::multimap<int, std::string>));
static_assert(srl::schema_valid(^^std::unordered_set<int>));
static_assert(srl::schema_valid(^^std::unordered_multiset<int>));
static_assert(srl::schema_valid(^^std::unordered_map<int, std::string>));
static_assert(srl::schema_valid(^^std::unordered_multimap<int, std::string>));
static_assert(srl::schema_valid(^^VectorField));
static_assert(srl::schema_valid(^^RejectingSetField));
static_assert(srl::schema_valid(^^TrackingSequence));
static_assert(!srl::schema_valid(^^ThirdPartyTrivial));
static_assert(!srl::leaf_serializable<ThirdPartyTrivial>);
static_assert(srl::schema_valid(^^ForcedLeafField));
static_assert(!srl::schema_valid(^^ForcedPointerField));
static_assert(srl::schema_valid(^^BypassedPointerField));
static_assert(srl::schema_valid(^^RawArrayField));
static_assert(!srl::leaf_serializable<std::string>);
static_assert(srl::container_serializable<std::string>);

static_assert(!std::has_unique_object_representations_v<PaddedLeaf>);
static_assert(srl::leaf_codec<PaddedLeaf>::uses_direct_object_representation);
static_assert(srl::leaf_codec<PaddedLeaf>::fixed_size == sizeof(PaddedLeaf));
static_assert(std::has_unique_object_representations_v<UniqueLeaf>);
static_assert(srl::leaf_codec<UniqueLeaf>::uses_direct_object_representation);
static_assert(srl::leaf_codec<UniqueLeaf>::fixed_size == sizeof(UniqueLeaf));
static_assert(!std::is_trivially_copyable_v<NonTrivialMemberwiseLeaf>);
static_assert(!srl::leaf_codec<TransientLeaf>::uses_direct_object_representation);
static_assert(srl::leaf_codec<TransientLeaf>::uses_segmented_object_representation);
static_assert(srl::leaf_codec<TransientLeaf>::copy_ranges.size() == 2);
static_assert(srl::leaf_codec<TransientLeaf>::fixed_size == sizeof(std::uint32_t) * 4);
static_assert(srl::leaf_codec<TransientPointerLeaf>::uses_segmented_object_representation);
static_assert(srl::leaf_codec<TransientPointerLeaf>::fixed_size
              == sizeof(TransientPointerLeaf) - sizeof(int *));
static_assert(srl::type_hash(^^Vec2Alias) == srl::type_hash(^^Vec2));
static_assert(srl::type_hash(^^int) != srl::type_hash(^^unsigned int));
static_assert(srl::enum_name(activity::running) == "running");
static_assert(srl::message(srl::error_code::missing_field) == "missing_field");

// Metadata APIs accept reflection values without first instantiating a typed wrapper.
static_assert(srl::schema_valid(^^const Person &));
static_assert(srl::has_serialized_fields(^^const Person &));
static_assert(srl::serialized_field_count(^^const Person &) == srl::serialized_field_count(^^Person));
static_assert(srl::detail::scalar_wire_type(^^bool) == 0x100u);
static_assert(srl::detail::scalar_wire_type(^^typename std::int32_t) == 0x204u);
static_assert(srl::detail::scalar_wire_type(^^typename std::uint64_t) == 0x308u);
static_assert(srl::detail::scalar_wire_type(^^double) == 0x408u);
static_assert(srl::detail::initialized_fallback_valid(^^int[2][3]));
static_assert(!srl::detail::initialized_fallback_valid(^^const int[2][3]));
static_assert(!srl::detail::initialized_fallback_valid(^^int[]));

namespace field_build_tests {
    constexpr auto maximum_id = std::numeric_limits<srl::id_type>::max();

    struct [[=srl::tag::auto_seq{}]] mixed_ids {
        int first{};
        [[=srl::tag::transient{}]] int *cache{};
        [[=srl::tag::field{10}]] int explicit_id{};
        int following{};
        [[=srl::tag::field{2}]] int reset{};
        int after_reset{};
    };

    constexpr auto mixed = srl::schema(^^mixed_ids).tree;
    constexpr auto mixed_value = mixed.value_index();
    static_assert(mixed.child_indices(mixed_value).size() == 5);
    static_assert(mixed[mixed_value, 0].record_field().id == 0);
    static_assert(mixed[mixed_value, 1].record_field().id == 2);
    static_assert(mixed[mixed_value, 2].record_field().id == 3);
    static_assert(mixed[mixed_value, 3].record_field().id == 10);
    static_assert(mixed[mixed_value, 4].record_field().id == 11);
    static_assert(mixed[mixed_value, 1].field().entity == ^^mixed_ids::reset);

    struct [[=srl::tag::auto_seq{}]] terminal_id {
        [[=srl::tag::field{maximum_id}]] int last{};
        [[=srl::tag::transient{}]] int skipped{};
    };
    struct [[=srl::tag::auto_seq{}]] reset_after_maximum {
        [[=srl::tag::field{maximum_id}]] int last{};
        [[=srl::tag::field{0}]] int reset{};
        int following{};
    };
    static_assert(srl::schema_valid(^^terminal_id));
    static_assert(srl::schema_valid(^^reset_after_maximum));

    struct [[=srl::tag::auto_seq{}]] overflow_before_unsupported {
        [[=srl::tag::field{maximum_id}]] int last{};
        unsigned bits : 1;
    };
    constexpr auto overflow = srl::inspect_schema(^^overflow_before_unsupported);
    static_assert(overflow.reason == srl::schema_error::field_id_overflow);
    static_assert(overflow.entity == ^^overflow_before_unsupported::bits);
    static_assert(overflow.path.size() == 2);
    static_assert(overflow.path[0] == ^^overflow_before_unsupported);
    static_assert(overflow.path[1] == ^^overflow_before_unsupported::bits);

    struct leaf_before_pointer {
        [[=srl::tag::field{0}, =srl::tag::leaf{}]] int *pointer{};
    };
    struct conflict_before_conversion {
        [[=srl::tag::field{0}, =srl::tag::leaf{}, =srl::conversion_tag{^^int}]] int *pointer{};
    };
    static_assert(srl::inspect_schema(^^leaf_before_pointer).reason
        == srl::schema_error::invalid_object_representation);
    static_assert(srl::inspect_schema(^^conflict_before_conversion).reason
        == srl::schema_error::conflicting_annotations);

    struct [[=srl::tag::auto_seq{}]] stream_override {
        [[=srl::tag::field{maximum_id}, =srl::tag::field{0}]] int first{};
        [[=srl::tag::field{maximum_id}]] int second{};
        [[=srl::tag::transient{}, =srl::tag::field{0}]] int skipped{};
    };
    struct stream_owner {
        [[=srl::tag::field{7}, =srl::tag::auto_stream{}]] stream_override value;
    };
    constexpr auto overridden = srl::schema(^^stream_owner).tree;
    constexpr auto owner = overridden.value_index();
    constexpr auto stream = overridden.child_indices(owner)[0];
    constexpr auto second = overridden.child_indices(stream)[1];

    static_assert(std::same_as<decltype(overridden[stream]), srl::schema_node>);
    static_assert(std::same_as<decltype(overridden[owner, 0]), srl::schema_node>);
    static_assert(std::same_as<decltype(overridden[owner, 0, 1u]), srl::schema_node>);
    static_assert(overridden[owner, 0].kind() == srl::schema_kind::stream);
    static_assert(overridden.child_indices(stream).size() == 2);
    static_assert(!overridden[owner, 0, 0].field_id().has_value());
    static_assert(!overridden[owner, 0, 1].field_id().has_value());
    static_assert(overridden[owner, 0, 1u].field().entity == overridden[second].field().entity);
    static_assert(overridden[srl::schema_tree::root_index, 0, 0, 1].field().entity == ^^stream_override::second);
    constexpr auto stream_field = overridden[owner, 0, 0];
    static_assert(stream_field.binding_kind() == srl::schema_binding_kind::stream_field);
    static_assert(stream_field.stream_field().entity == ^^stream_override::first);
}

namespace {
    struct [[=srl::tag::auto_seq{}]] constructed_record {
        int value;
    };
    struct [[=srl::tag::auto_stream{}]] constructed_stream {
        int value;
    };

    constexpr srl::schema record_description(^^constructed_record);
    constexpr srl::schema stream_description(^^constructed_stream);
    constexpr srl::schema typed_description(std::in_place_type<const constructed_stream &>);
    constexpr srl::schema forced_description(^^constructed_record, true);
    static_assert(record_description.value_node().kind() == srl::schema_kind::record);
    static_assert(stream_description.value_node().kind() == srl::schema_kind::stream);
    static_assert(typed_description.value_node().kind() == srl::schema_kind::stream);
    static_assert(forced_description.value_node().kind() == srl::schema_kind::stream);
}

namespace {
    struct annotated_field {
        [[=srl::tag::field{0}, =srl::tag::fallback{}, =srl::conversion_tag{^^int}]]
        int value = 3;
    };

    constexpr auto annotated_tree = srl::schema(^^annotated_field).tree;
    constexpr auto annotated = annotated_tree[annotated_tree.value_index(), 0];
    static_assert(annotated.kind() == srl::schema_kind::leaf);
    static_assert(annotated.field_id() == 0);
    static_assert(annotated.record_field().fallback_type().has_value());
    static_assert(annotated.converted() && annotated.field().conversion == srl::schema_conversion_kind::cast);
    static_assert(annotated.type() == annotated.field().memory_type());

    constexpr auto sequence = srl::schema(std::in_place_type<std::vector<int>>).tree;
    constexpr auto map = srl::schema(std::in_place_type<std::map<int, long>>).tree;
    static_assert(sequence.value_node().container().shape == srl::schema_container_shape::sequence);
    static_assert(sequence.child_indices(sequence.value_index()).size() == 1);
    static_assert(sequence[sequence.value_index(), 0].slot().role == srl::schema_slot_role::element);
    static_assert(map.value_node().container().shape == srl::schema_container_shape::map);
    static_assert(map.child_indices(map.value_index()).size() == 2);
    static_assert(map[map.value_index(), 0].slot().role == srl::schema_slot_role::key);
    static_assert(map[map.value_index(), 1].slot().role == srl::schema_slot_role::mapped);
}
