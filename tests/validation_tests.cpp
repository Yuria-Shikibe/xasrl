#include "test_support.hpp"

#include <array>
#include <concepts>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace validation_tests {
    enum class state : std::uint8_t { idle, active };
    enum class boolean : bool { yes = true };
    enum class [[=srl::tag::bypass_validation{}]] trusted_state : std::uint8_t { idle };
    enum class [[=srl::tag::bit_flag{3}]] flags : std::uint8_t { first = 1, second = 2 };

    struct [[=srl::tag::leaf{}]] mixed {
        [[=srl::tag::bypass_validation{}]] state trusted{};
        state checked{};
    };
    struct [[=srl::tag::leaf{}, =srl::tag::bypass_validation{}]] trusted_leaf {
        bool enabled{};
        state states[2]{};
    };
    struct [[=srl::tag::leaf{.allow_non_unique_presentation = true}]] segmented {
        [[=srl::tag::bypass_validation{}]] state trusted{};
        [[=srl::tag::transient{}]] std::uint32_t cache = 42;
        state checked{};
    };
    struct [[=srl::tag::auto_seq{}]] fields {
        [[=srl::tag::bypass_validation{}]] state trusted{};
        state checked{};
    };
    struct [[=srl::tag::auto_stream{}]] stream {
        [[=srl::tag::bypass_validation{}]] flags trusted = flags::first;
        flags checked = flags::first;
    };
    struct [[=srl::tag::auto_seq{}, =srl::tag::bypass_validation{}]] trusted_record {
        std::vector<state> items;
        fields nested;
    };
    struct [[=srl::tag::auto_seq{}]] field_subtree {
        [[=srl::tag::bypass_validation{}]] std::vector<state> items;
        [[=srl::tag::bypass_validation{}]] mixed object;
        [[=srl::tag::leaf{}, =srl::tag::bypass_validation{}]] mixed forced;
    };
    struct base { state value{}; };
    struct [[=srl::tag::leaf{}]] trusted_base
        : [[=srl::tag::bypass_validation{}]] base {};
    struct [[=srl::tag::leaf{}]] boolean_object { boolean value = boolean::yes; };

    struct [[=srl::tag::leaf{}, =srl::tag::bypass_validation{}]] custom {
        std::uint8_t value{};
    };
    struct [[=srl::tag::auto_seq{}, =srl::tag::bypass_validation{}]] custom_parent { custom value; };

    struct [[=srl::tag::auto_seq{}]] duplicate_field {
        [[=srl::tag::bypass_validation{}, =srl::tag::bypass_validation{}]] state value;
    };
    struct [[=srl::tag::auto_seq{}, =srl::tag::bypass_validation{}, =srl::tag::bypass_validation{}]] duplicate_type {
        state value;
    };
    struct ignored_field {
        [[=srl::tag::field{0}]] int serialized;
        [[=srl::tag::bypass_validation{}]] state ignored;
    };
    struct [[=srl::tag::leaf{}]] transient_conflict {
        [[=srl::tag::transient{}, =srl::tag::bypass_validation{}]] int value;
    };
    enum class [[=srl::tag::bit_flag{1}, =srl::tag::bypass_validation{}]] invalid_flags : unsigned { bad = 2 };
    union invalid_union { int integer; float real; };
    struct [[=srl::tag::leaf{}, =srl::tag::bypass_validation{}]] invalid_object { invalid_union value; };
}

template<>
struct srl::leaf_codec<validation_tests::custom> {
    static constexpr std::size_t fixed_size = 1;
    static srl::always_success encode(const validation_tests::custom &value,
                                     std::span<std::byte, fixed_size> bytes) noexcept {
        bytes[0] = static_cast<std::byte>(value.value);
        return {};
    }
    static srl::error decode(std::span<const std::byte, fixed_size> bytes,
                             validation_tests::custom &out) noexcept {
        if (bytes[0] != std::byte{0}) { return srl::detail::fail(srl::error_code::invalid_enum_value); }
        out.value = 0;
        return {};
    }
};

namespace validation_tests {
    template<typename T>
    using decode_status = decltype(srl::leaf_codec<T>::decode(
        std::declval<std::span<const std::byte, srl::leaf_codec<T>::fixed_size>>(), std::declval<T &>()));

    template<typename T>
    inline constexpr srl::schema field_schema(^^T);

    template<typename T, std::size_t Ordinal>
    using field_decode_status = typename srl::detail::value_codec<field_schema<T>.tree,
        field_schema<T>.tree.child_indices(field_schema<T>.tree.value_index())[Ordinal]>::payload_decode_result;

    static_assert(std::same_as<decode_status<int>, srl::always_success>);
    static_assert(std::same_as<decode_status<bool>, srl::error>);
    static_assert(std::same_as<decode_status<state>, srl::error>);
    static_assert(std::same_as<decode_status<trusted_state>, srl::always_success>);
    static_assert(std::same_as<decode_status<trusted_leaf>, srl::always_success>);
    static_assert(std::same_as<decode_status<trusted_base>, srl::always_success>);
    static_assert(std::same_as<decode_status<mixed>, srl::error>);
    static_assert(std::same_as<decode_status<segmented>, srl::error>);
    static_assert(std::same_as<field_decode_status<fields, 0>, srl::always_success>);
    static_assert(std::same_as<field_decode_status<fields, 1>, srl::error>);
    static_assert(std::same_as<field_decode_status<field_subtree, 1>, srl::always_success>);
    static_assert(std::same_as<field_decode_status<field_subtree, 2>, srl::always_success>);
    static_assert(std::same_as<field_decode_status<custom_parent, 0>, srl::error>);
    static_assert(!srl::schema_valid(^^duplicate_field));
    static_assert(!srl::schema_valid(^^duplicate_type));
    static_assert(!srl::schema_valid(^^ignored_field));
    static_assert(!srl::schema_valid(^^transient_conflict));
    static_assert(!srl::schema_valid(^^invalid_flags));
    static_assert(!srl::schema_valid(^^invalid_object));
    constexpr srl::schema fields_description(^^fields);
    static_assert(fields_description.tree[fields_description.tree.value_index(), 0].wire_type()
        == fields_description.tree[fields_description.tree.value_index(), 1].wire_type());

    TEST(Validation, ScalarPoliciesRejectBeforeAssignment) {
        std::array bytes{std::byte{2}};
        bool boolean_out = true;
        EXPECT_EQ(srl::leaf_codec<bool>::decode(bytes, boolean_out).code, srl::error_code::invalid_bool);
        EXPECT_TRUE(boolean_out);
        boolean enum_out = boolean::yes;
        EXPECT_EQ(srl::leaf_codec<boolean>::decode(bytes, enum_out).code, srl::error_code::invalid_enum_value);
        EXPECT_EQ(enum_out, boolean::yes);
        bytes[0] = std::byte{0};
        EXPECT_EQ(srl::leaf_codec<boolean>::decode(bytes, enum_out).code, srl::error_code::invalid_enum_value);
        bytes[0] = std::byte{1};
        EXPECT_FALSE(srl::leaf_codec<boolean>::decode(bytes, enum_out));

        trusted_state trusted{};
        bytes[0] = std::byte{7};
        EXPECT_FALSE(srl::leaf_codec<trusted_state>::decode(bytes, trusted));
        EXPECT_EQ(std::to_underlying(trusted), 7);

        boolean_object object;
        EXPECT_EQ(srl::leaf_codec<boolean_object>::decode(bytes, object).code, srl::error_code::invalid_enum_value);
        EXPECT_EQ(object.value, boolean::yes);
    }

    template<typename T>
    void check_object_fields() {
        constexpr auto layout = srl::leaf_codec<T>::layout;
        constexpr auto trusted_offset = layout.fields()[0].wire_offset;
        constexpr auto checked_offset = layout.fields()[1].wire_offset;
        std::array<std::byte, srl::leaf_codec<T>::fixed_size> bytes{};
        bytes[trusted_offset] = std::byte{7};
        T out;
        ASSERT_FALSE(srl::leaf_codec<T>::decode(bytes, out));
        EXPECT_EQ(std::to_underlying(out.trusted), 7);
        EXPECT_EQ(out.checked, state::idle);
        bytes[checked_offset] = std::byte{8};
        out.trusted = state::active;
        const auto failure = srl::leaf_codec<T>::decode(bytes, out);
        EXPECT_EQ(failure.code, srl::error_code::invalid_enum_value);
        EXPECT_EQ(failure.offset, checked_offset);
        EXPECT_EQ(out.trusted, state::active);
        EXPECT_EQ(out.checked, state::idle);
        if constexpr (std::same_as<T, segmented>) { EXPECT_EQ(out.cache, 42u); }
    }

    TEST(Validation, ObjectFieldsUseIndependentPoliciesAndPreserveFailureAtomicity) {
        check_object_fields<mixed>();
        check_object_fields<segmented>();
    }

    TEST(Validation, TypeAndFieldAnnotationsPropagateThroughSubtrees) {
        const auto unknown = static_cast<state>(7);
        const trusted_record source{{unknown}, {unknown, unknown}};
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(source, bytes, context));
        trusted_record out;
        ASSERT_FALSE(srl::deserialize(bytes.span(), out, context));
        ASSERT_EQ(out.items.size(), 1u);
        EXPECT_EQ(out.items[0], unknown);
        EXPECT_EQ(out.nested.checked, unknown);

        const field_subtree scoped{{unknown}, {unknown, unknown}, {unknown, unknown}};
        ASSERT_FALSE(srl::serialize(scoped, bytes, context));
        field_subtree scoped_out;
        ASSERT_FALSE(srl::deserialize(bytes.span(), scoped_out, context));
        EXPECT_EQ(scoped_out.items, scoped.items);
        EXPECT_EQ(scoped_out.object.checked, unknown);
        EXPECT_EQ(scoped_out.forced.checked, unknown);

        const trusted_leaf leaf{true, {unknown, unknown}};
        ASSERT_FALSE(srl::serialize(leaf, bytes, context));
        trusted_leaf leaf_out;
        ASSERT_FALSE(srl::deserialize(bytes.span(), leaf_out, context));
        EXPECT_TRUE(leaf_out.enabled);
        EXPECT_EQ(leaf_out.states[1], unknown);

        trusted_base inherited;
        inherited.value = unknown;
        ASSERT_FALSE(srl::serialize(inherited, bytes, context));
        trusted_base inherited_out;
        ASSERT_FALSE(srl::deserialize(bytes.span(), inherited_out, context));
        EXPECT_EQ(inherited_out.value, unknown);
    }

    TEST(Validation, RecordSiblingsKeepValidationAndAbsoluteDiagnostics) {
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(fields{static_cast<state>(7), state::idle}, bytes, context));
        fields out;
        ASSERT_FALSE(srl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(std::to_underlying(out.trusted), 7);
        const auto entry = srl_test::open_document(bytes.span());
        const auto offset = entry.at(1).offset;
        bytes[offset] = std::byte{8};
        const auto failure = srl::deserialize(bytes.span(), out, context);
        EXPECT_EQ(failure.code, srl::error_code::invalid_enum_value);
        EXPECT_EQ(failure.id, 1u);
        EXPECT_EQ(failure.offset, offset);
    }

    TEST(Validation, StreamFlagsUseFieldPolicy) {
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(stream{static_cast<flags>(4), flags::first}, bytes, context));
        stream out;
        ASSERT_FALSE(srl::deserialize(bytes.span(), out, context));
        EXPECT_EQ(std::to_underlying(out.trusted), 4);
        ASSERT_FALSE(srl::serialize(stream{flags::first, static_cast<flags>(4)}, bytes, context));
        EXPECT_EQ(srl::deserialize(bytes.span(), out, context).code, srl::error_code::invalid_enum_value);
    }

    TEST(Validation, StructuralParsingAndCustomCodecsRemainChecked) {
        srl::serialization_context context;
        srl::byte_buffer bytes;
        ASSERT_FALSE(srl::serialize(trusted_state::idle, bytes, context));
        trusted_state out{};
        EXPECT_TRUE(srl::deserialize(bytes.span().first(bytes.size() - 1), out, context));

        // The fixed-size payload contract remains checked even for an unchecked value.
        static constexpr srl::schema description(^^trusted_state);
        using codec = srl::detail::value_codec<description.tree, description.tree.value_index()>;
        const std::span<const std::byte> empty;
        EXPECT_EQ(codec::decode(empty, out, context).code, srl::error_code::invalid_payload_shape);

        ASSERT_FALSE(srl::serialize(trusted_record{{state::idle}, {}}, bytes, context));
        context.config.limits.max_input_bytes = bytes.size() - 1;
        trusted_record limited_out;
        EXPECT_TRUE(srl::deserialize(bytes.span(), limited_out, context));
        context.config.limits = {};

        ASSERT_FALSE(srl::serialize(custom_parent{custom{7}}, bytes, context));
        custom_parent custom_out;
        EXPECT_EQ(srl::deserialize(bytes.span(), custom_out, context).code, srl::error_code::invalid_enum_value);
    }
}
