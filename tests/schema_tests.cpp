#include "srl/error.hpp"

#include <gtest/gtest.h>
#include <concepts>
#include <limits>
#include <map>
#include <vector>

#include "test_models.hpp"

static_assert(xasrl::schema_valid(^^Person));
static_assert(xasrl::schema_valid(^^Vec2));
static_assert(xasrl::schema_valid(^^Rect));
static_assert(xasrl::schema_valid(^^PaddedLeaf));
static_assert(xasrl::schema_valid(^^UniqueLeaf));
static_assert(!xasrl::schema_valid(^^NonTrivialMemberwiseLeaf));
static_assert(xasrl::schema_valid(^^TransientLeaf));
static_assert(xasrl::schema_valid(^^TransientPointerLeaf));
static_assert(!xasrl::schema_valid(^^InvalidRecordTransient));
static_assert(!xasrl::schema_valid(^^DerivedLeaf));
static_assert(!xasrl::schema_valid(^^PrivateDerivedLeaf));
static_assert(!xasrl::schema_valid(^^ProtectedDerivedLeaf));
static_assert(!xasrl::schema_valid(^^VirtualDerivedLeaf));
static_assert(!xasrl::schema_valid(^^NonTrivialPaddedLeaf));
static_assert(xasrl::schema_valid(^^SmallText));
static_assert(xasrl::schema_valid(^^AutoSequenceBase));
static_assert(xasrl::schema_valid(^^AutoSequenceRecord));
static_assert(!xasrl::schema_valid(^^AutoSequenceDuplicate));
static_assert(!xasrl::schema_valid(^^AutoSequenceOverflow));
static_assert(xasrl::schema_valid(^^AutoSequenceEntityBypass));
static_assert(xasrl::schema_valid(^^AutoSequenceTypeBypass));
static_assert(!xasrl::schema_valid(^^AutoSequenceRejectedPointer));
static_assert(xasrl::schema_valid(^^LeafEntityBypass));
static_assert(xasrl::schema_valid(^^LeafBaseBypass));
static_assert(!xasrl::leaf_serializable<RuntimeFixedSize>);
static_assert(!xasrl::schema_valid(^^RuntimeFixedSize));
static_assert(!xasrl::leaf_serializable<DynamicSpanCodec>);
static_assert(!xasrl::schema_valid(^^DynamicSpanCodec));
static_assert(xasrl::schema_valid(^^Profile));
static_assert(xasrl::schema_valid(^^Pair));
static_assert(xasrl::schema_valid(^^FirstOnly));
static_assert(!xasrl::schema_valid(^^DuplicateIds));
static_assert(xasrl::inspect_schema(^^DuplicateIds).reason == xasrl::schema_error::duplicate_field_id);
static_assert(xasrl::schema_diagnostic(^^DuplicateIds).contains("duplicate_field_id"));
static_assert(xasrl::inspect_schema(^^MarkedPointer).reason == xasrl::schema_error::unsupported_entity);
static_assert(!xasrl::schema_valid(^^MarkedPointer));
static_assert(!xasrl::schema_valid(^^LeafPointer));
static_assert(xasrl::schema_valid(^^BypassedLeafPointer));
static_assert(!xasrl::schema_valid(^^UnsupportedVariableLeaf));
static_assert(!xasrl::schema_valid(^^AmbiguousLeaf));
static_assert(xasrl::schema_valid(^^int[3]));
static_assert(!xasrl::schema_valid(^^int *[3]));
static_assert(xasrl::schema_valid(^^std::array<int, 3>));
static_assert(xasrl::container_traits<std::array<int, 3>>::fixed_size == 3);
static_assert(xasrl::container_traits<int[3]>::fixed_size == 3);
static_assert(xasrl::container_traits<FixedSequence<4>>::fixed_size == 4);
static_assert(xasrl::container_traits<std::vector<int>>::fixed_size == std::dynamic_extent);
static_assert(xasrl::container_traits<std::span<int, 4>>::fixed_size == 4);
static_assert(!xasrl::container_serializable<std::span<int, 4>>);
static_assert(xasrl::schema_valid(^^FixedSequence<4>));
static_assert(xasrl::schema_valid(^^std::vector<int>));
static_assert(xasrl::schema_valid(^^std::deque<std::string>));
static_assert(xasrl::schema_valid(^^std::list<Person>));
static_assert(xasrl::schema_valid(^^std::forward_list<int>));
static_assert(xasrl::schema_valid(^^std::set<int>));
static_assert(xasrl::schema_valid(^^std::multiset<int>));
static_assert(xasrl::schema_valid(^^std::map<int, std::string>));
static_assert(xasrl::schema_valid(^^std::multimap<int, std::string>));
static_assert(xasrl::schema_valid(^^std::unordered_set<int>));
static_assert(xasrl::schema_valid(^^std::unordered_multiset<int>));
static_assert(xasrl::schema_valid(^^std::unordered_map<int, std::string>));
static_assert(xasrl::schema_valid(^^std::unordered_multimap<int, std::string>));
static_assert(xasrl::schema_valid(^^VectorField));
static_assert(xasrl::schema_valid(^^RejectingSetField));
static_assert(xasrl::schema_valid(^^TrackingSequence));
static_assert(!xasrl::schema_valid(^^ThirdPartyTrivial));
static_assert(!xasrl::leaf_serializable<ThirdPartyTrivial>);
static_assert(xasrl::schema_valid(^^ForcedLeafField));
static_assert(!xasrl::schema_valid(^^ForcedPointerField));
static_assert(xasrl::schema_valid(^^BypassedPointerField));
static_assert(xasrl::schema_valid(^^RawArrayField));
static_assert(!xasrl::leaf_serializable<std::string>);
static_assert(xasrl::container_serializable<std::string>);

static_assert(!std::has_unique_object_representations_v<PaddedLeaf>);
static_assert(xasrl::leaf_codec<PaddedLeaf>::uses_direct_object_representation);
static_assert(xasrl::leaf_codec<PaddedLeaf>::fixed_size == sizeof(PaddedLeaf));
static_assert(std::has_unique_object_representations_v<UniqueLeaf>);
static_assert(xasrl::leaf_codec<UniqueLeaf>::uses_direct_object_representation);
static_assert(xasrl::leaf_codec<UniqueLeaf>::fixed_size == sizeof(UniqueLeaf));
static_assert(!std::is_trivially_copyable_v<NonTrivialMemberwiseLeaf>);
static_assert(!xasrl::leaf_codec<TransientLeaf>::uses_direct_object_representation);
static_assert(xasrl::leaf_codec<TransientLeaf>::uses_segmented_object_representation);
static_assert(xasrl::leaf_codec<TransientLeaf>::copy_ranges.size() == 2);
static_assert(xasrl::leaf_codec<TransientLeaf>::fixed_size == sizeof(std::uint32_t) * 4);
static_assert(xasrl::leaf_codec<TransientPointerLeaf>::uses_segmented_object_representation);
static_assert(xasrl::leaf_codec<TransientPointerLeaf>::fixed_size
              == sizeof(TransientPointerLeaf) - sizeof(int *));
static_assert(xasrl::type_hash(^^Vec2Alias) == xasrl::type_hash(^^Vec2));
static_assert(xasrl::type_hash(^^int) != xasrl::type_hash(^^unsigned int));
static_assert(xasrl::enum_name(activity::running) == "running");
static_assert(xasrl::message(xasrl::error_code::missing_field) == "missing_field");

// Metadata APIs accept reflection values without first instantiating a typed wrapper.
static_assert(xasrl::schema_valid(^^const Person &));
static_assert(xasrl::has_serialized_fields(^^const Person &));
static_assert(xasrl::serialized_field_count(^^const Person &) == xasrl::serialized_field_count(^^Person));
static_assert(xasrl::detail::scalar_wire_type(^^bool) == 0x100u);
static_assert(xasrl::detail::scalar_wire_type(^^typename std::int32_t) == 0x204u);
static_assert(xasrl::detail::scalar_wire_type(^^typename std::uint64_t) == 0x308u);
static_assert(xasrl::detail::scalar_wire_type(^^double) == 0x408u);
static_assert(xasrl::detail::initialized_fallback_valid(^^int[2][3]));
static_assert(!xasrl::detail::initialized_fallback_valid(^^const int[2][3]));
static_assert(!xasrl::detail::initialized_fallback_valid(^^int[]));

namespace field_build_tests {
    constexpr auto maximum_id = std::numeric_limits<xasrl::id_type>::max();

    struct [[=xasrl::tag::auto_seq{}]] mixed_ids {
        int first{};
        [[=xasrl::tag::transient{}]] int *cache{};
        [[=xasrl::tag::field{10}]] int explicit_id{};
        int following{};
        [[=xasrl::tag::field{2}]] int reset{};
        int after_reset{};
    };

    constexpr auto mixed = xasrl::schema(^^mixed_ids).tree;
    constexpr auto mixed_value = mixed.value_index();
    static_assert(mixed.child_indices(mixed_value).size() == 5);
    static_assert(mixed[mixed_value, 0].record_field().id == 0);
    static_assert(mixed[mixed_value, 1].record_field().id == 2);
    static_assert(mixed[mixed_value, 2].record_field().id == 3);
    static_assert(mixed[mixed_value, 3].record_field().id == 10);
    static_assert(mixed[mixed_value, 4].record_field().id == 11);
    static_assert(mixed[mixed_value, 1].field().entity == ^^mixed_ids::reset);

    struct [[=xasrl::tag::auto_seq{}]] terminal_id {
        [[=xasrl::tag::field{maximum_id}]] int last{};
        [[=xasrl::tag::transient{}]] int skipped{};
    };
    struct [[=xasrl::tag::auto_seq{}]] reset_after_maximum {
        [[=xasrl::tag::field{maximum_id}]] int last{};
        [[=xasrl::tag::field{0}]] int reset{};
        int following{};
    };
    static_assert(xasrl::schema_valid(^^terminal_id));
    static_assert(xasrl::schema_valid(^^reset_after_maximum));

    struct [[=xasrl::tag::auto_seq{}]] overflow_before_unsupported {
        [[=xasrl::tag::field{maximum_id}]] int last{};
        unsigned bits : 1;
    };
    constexpr auto overflow = xasrl::inspect_schema(^^overflow_before_unsupported);
    static_assert(overflow.reason == xasrl::schema_error::field_id_overflow);
    static_assert(overflow.entity == ^^overflow_before_unsupported::bits);
    static_assert(overflow.path.size() == 2);
    static_assert(overflow.path[0] == ^^overflow_before_unsupported);
    static_assert(overflow.path[1] == ^^overflow_before_unsupported::bits);

    struct leaf_before_pointer {
        [[=xasrl::tag::field{0}, =xasrl::tag::leaf{}]] int *pointer{};
    };
    struct conflict_before_conversion {
        [[=xasrl::tag::field{0}, =xasrl::tag::leaf{}, =xasrl::conversion_tag{^^int}]] int *pointer{};
    };
    static_assert(xasrl::inspect_schema(^^leaf_before_pointer).reason
        == xasrl::schema_error::invalid_object_representation);
    static_assert(xasrl::inspect_schema(^^conflict_before_conversion).reason
        == xasrl::schema_error::conflicting_annotations);

    struct [[=xasrl::tag::auto_seq{}]] stream_override {
        [[=xasrl::tag::field{maximum_id}, =xasrl::tag::field{0}]] int first{};
        [[=xasrl::tag::field{maximum_id}]] int second{};
        [[=xasrl::tag::transient{}, =xasrl::tag::field{0}]] int skipped{};
    };
    struct stream_owner {
        [[=xasrl::tag::field{7}, =xasrl::tag::auto_stream{}]] stream_override value;
    };
    constexpr auto overridden = xasrl::schema(^^stream_owner).tree;
    constexpr auto owner = overridden.value_index();
    constexpr auto stream = overridden.child_indices(owner)[0];
    constexpr auto second = overridden.child_indices(stream)[1];

    static_assert(std::same_as<decltype(overridden[stream]), xasrl::schema_node>);
    static_assert(std::same_as<decltype(overridden[owner, 0]), xasrl::schema_node>);
    static_assert(std::same_as<decltype(overridden[owner, 0, 1u]), xasrl::schema_node>);
    static_assert(overridden[owner, 0].kind() == xasrl::schema_kind::stream);
    static_assert(overridden.child_indices(stream).size() == 2);
    static_assert(!overridden[owner, 0, 0].field_id().has_value());
    static_assert(!overridden[owner, 0, 1].field_id().has_value());
    static_assert(overridden[owner, 0, 1u].field().entity == overridden[second].field().entity);
    static_assert(overridden[xasrl::schema_tree::root_index, 0, 0, 1].field().entity == ^^stream_override::second);
    constexpr auto stream_field = overridden[owner, 0, 0];
    static_assert(stream_field.binding_kind() == xasrl::schema_binding_kind::stream_field);
    static_assert(stream_field.stream_field().entity == ^^stream_override::first);
}

namespace {
    struct [[=xasrl::tag::auto_seq{}]] constructed_record {
        int value;
    };
    struct [[=xasrl::tag::auto_stream{}]] constructed_stream {
        int value;
    };

    constexpr xasrl::schema record_description(^^constructed_record);
    constexpr xasrl::schema stream_description(^^constructed_stream);
    constexpr xasrl::schema typed_description(std::in_place_type<const constructed_stream &>);
    constexpr xasrl::schema forced_description(^^constructed_record, true);
    static_assert(record_description.value_node().kind() == xasrl::schema_kind::record);
    static_assert(stream_description.value_node().kind() == xasrl::schema_kind::stream);
    static_assert(typed_description.value_node().kind() == xasrl::schema_kind::stream);
    static_assert(forced_description.value_node().kind() == xasrl::schema_kind::stream);
}

namespace {
    struct annotated_field {
        [[=xasrl::tag::field{0}, =xasrl::tag::fallback{}, =xasrl::conversion_tag{^^int}]]
        int value = 3;
    };

    constexpr auto annotated_tree = xasrl::schema(^^annotated_field).tree;
    constexpr auto annotated = annotated_tree[annotated_tree.value_index(), 0];
    static_assert(annotated.kind() == xasrl::schema_kind::leaf);
    static_assert(annotated.field_id() == 0);
    static_assert(annotated.record_field().fallback_type().has_value());
    static_assert(annotated.converted() && annotated.field().conversion == xasrl::schema_conversion_kind::cast);
    static_assert(annotated.type() == annotated.field().memory_type());

    constexpr auto sequence = xasrl::schema(std::in_place_type<std::vector<int>>).tree;
    constexpr auto map = xasrl::schema(std::in_place_type<std::map<int, long>>).tree;
    static_assert(sequence.value_node().container().shape == xasrl::schema_container_shape::sequence);
    static_assert(sequence.child_indices(sequence.value_index()).size() == 1);
    static_assert(sequence[sequence.value_index(), 0].slot().role == xasrl::schema_slot_role::element);
    static_assert(map.value_node().container().shape == xasrl::schema_container_shape::map);
    static_assert(map.child_indices(map.value_index()).size() == 2);
    static_assert(map[map.value_index(), 0].slot().role == xasrl::schema_slot_role::key);
    static_assert(map[map.value_index(), 1].slot().role == xasrl::schema_slot_role::mapped);
}
