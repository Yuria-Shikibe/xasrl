#include "test_support.hpp"

namespace stream_tests {
    using namespace srl_test;

    struct Plain {
        std::uint32_t count{};
        std::string name;
    };

    struct [[=srl::tag::auto_stream{}]] Empty {};
    struct [[=srl::tag::auto_stream{}]] Numbers {
        [[=srl::tag::field{9}]] std::uint32_t first{};
        [[=srl::tag::field{9}]] std::uint16_t second{};
        [[=srl::tag::transient{}]] void *cache{};
    };
    struct [[=srl::tag::auto_seq{}]] Sequence {
        std::uint32_t first{};
        std::uint16_t second{};
    };
    struct [[=srl::tag::auto_stream{}]] Nested
        : [[=srl::tag::auto_stream{}]] Plain {
        [[=srl::tag::auto_stream{}]] Plain local;
        Sequence record;
        Numbers stream;
        std::vector<Numbers> items;
    };
    struct Record {
        [[=srl::tag::field{7}]] [[=srl::tag::auto_stream{}]] Plain local;
        [[=srl::tag::field{8}]] Numbers stream;
        [[=srl::tag::field{9}]] [[=srl::tag::auto_stream{}]] Sequence overridden;
    };
    struct Runtime {
        static inline int writes{};
        std::uint32_t count{};
        explicit operator Plain() const { ++writes; return {count, "converted"}; }
        Runtime() = default;
        explicit Runtime(Plain value) : count(value.count) {}
    };
    struct [[=srl::tag::auto_stream{}]] Converted {
        [[=srl::conversion_tag{^^Plain}]] [[=srl::tag::auto_stream{}]] Runtime value;
    };
    struct [[=srl::tag::auto_stream{}]] Conflict {
        [[=srl::tag::auto_stream{}]] int value;
    };
    struct [[=srl::tag::auto_stream{}]] ContainerConflict {
        [[=srl::tag::auto_stream{}]] std::vector<int> value;
    };
    struct [[=srl::tag::auto_stream{}]] LeafConflict {
        [[=srl::tag::auto_stream{}]] UniqueLeaf value;
    };
    struct [[=srl::tag::auto_stream{}]] [[=srl::tag::auto_seq{}]] Modes {};
    struct [[=srl::tag::auto_stream{}]] [[=srl::tag::auto_stream{}]] Duplicate {};
    struct [[=srl::tag::auto_stream{}]] Pointer { int *value; };
    struct [[=srl::tag::auto_stream{}]] Reference { int &value; };
    struct [[=srl::tag::auto_stream{}]] Bits { unsigned value : 1; };
    struct [[=srl::tag::auto_stream{}]] Virtual : virtual Plain {};
    struct [[=srl::tag::auto_stream{}]] Missing { Plain value; };
    struct [[=srl::tag::auto_stream{}]] Recursive { std::vector<Recursive> value; };
    struct MissingField { [[=srl::tag::auto_stream{}]] Plain value; };
    struct [[=srl::tag::auto_stream{}]] TransientConflict {
        [[=srl::tag::auto_stream{}]] [[=srl::tag::transient{}]] Plain value;
    };
    struct [[=srl::tag::auto_stream{}]] FieldLeafConflict {
        [[=srl::tag::auto_stream{}]] [[=srl::tag::leaf{}]] Sequence value;
    };
    struct [[=srl::tag::auto_stream{}]] RepeatedField {
        [[=srl::tag::auto_stream{}]] [[=srl::tag::auto_stream{}]] Plain value;
    };
    struct [[=srl::tag::leaf{}]] LeafOwner {
        [[=srl::tag::auto_stream{}]] std::uint32_t value;
    };
    struct [[=srl::tag::auto_stream{}]] ForcedLeaf {
        [[=srl::tag::leaf{.allow_non_unique_presentation = true}]] Sequence value;
        [[=srl::tag::bypass_pointer_check{}]] int *pointer{};
    };
    struct DuplicateRecord {
        [[=srl::tag::field{8}]] std::uint32_t first{};
        [[=srl::tag::field{8}]] std::uint16_t second{};
    };
    struct [[=srl::tag::auto_stream{}]] OverrideIds {
        [[=srl::tag::auto_stream{}]] DuplicateRecord value;
    };

    static_assert(srl::reflected_stream<Numbers> && !srl::reflected_record<Numbers>);
    static_assert(srl::schema_valid(^^Empty) && srl::schema_valid(^^Numbers));
    static_assert(srl::schema_valid(^^Nested) && srl::schema_valid(^^Record));
    static_assert(srl::schema_valid(^^Converted));
    static_assert(srl::serialized_field_count(^^Numbers) == 2);
    static_assert(!srl::schema_valid(^^Conflict) && !srl::schema_valid(^^ContainerConflict));
    static_assert(!srl::schema_valid(^^LeafConflict) && !srl::schema_valid(^^Modes));
    static_assert(!srl::schema_valid(^^Duplicate) && !srl::schema_valid(^^Pointer));
    static_assert(!srl::schema_valid(^^Reference) && !srl::schema_valid(^^Bits));
    static_assert(!srl::schema_valid(^^Virtual) && !srl::schema_valid(^^Missing));
    static_assert(!srl::schema_valid(^^Recursive) && !srl::schema_valid(^^MissingField));
    static_assert(!srl::schema_valid(^^TransientConflict) && !srl::schema_valid(^^FieldLeafConflict));
    static_assert(!srl::schema_valid(^^RepeatedField) && !srl::schema_valid(^^LeafOwner));
    static_assert(srl::schema_valid(^^ForcedLeaf) && srl::schema_valid(^^OverrideIds));

    TEST(AutoStream, WireLayoutAndApis) {
        srl::byte_buffer bytes{};
        const auto bytes_status = srl::serialize(Numbers{0x12345678, 0x1122}, bytes, default_context);
        srl::byte_buffer seq{};
        const auto seq_status = srl::serialize(Sequence{0x12345678, 0x1122}, seq, default_context);
        ASSERT_TRUE(!bytes_status && !seq_status);
        EXPECT_EQ(bytes.size(), 82u);
        EXPECT_EQ(seq.size(), bytes.size());
        const auto doc = open_document(bytes.span());
        EXPECT_EQ(doc.entry.head().kind(), srl::entry_kind::stream);
        EXPECT_EQ(doc.entry.head().mapping_count(), 0u);
        EXPECT_EQ(get_u32(bytes, doc.at(0).offset), 0x12345678u);
        EXPECT_EQ(std::to_integer<unsigned>(doc.at(1).bytes[0]), 0x22u);
        EXPECT_EQ(srl::serialized_size(Numbers{}, default_context), 82u);
        srl::byte_buffer into;
        into.resize(bytes.size());
        ASSERT_FALSE(srl::serialize(Numbers{0x12345678, 0x1122}, into.span(), default_context));
        EXPECT_EQ(into, bytes);
        int marker{};
        Numbers target{1, 2, &marker};
        ASSERT_FALSE(srl::deserialize(bytes.span(), target, default_context));
        EXPECT_EQ(target.first, 0x12345678u);
        EXPECT_EQ(target.cache, &marker);
        srl::byte_buffer empty{};
        const auto empty_status = srl::serialize(Empty{}, empty, default_context);
        ASSERT_TRUE(!empty_status);
        EXPECT_EQ(empty.size(), 44u);
        Empty empty_copy{};
        EXPECT_FALSE(srl::deserialize(empty.span(), empty_copy, default_context));
    }

    TEST(AutoStream, NestedOverridesAndFileRoundTrip) {
        Nested value;
        value.count = 11;
        value.name = "base";
        value.local = {22, "field"};
        value.record = {33, 44};
        value.stream = {55, 66};
        value.items = {{77, 88}, {99, 100}};
        srl::byte_buffer bytes{};
        const auto bytes_status = srl::serialize(value, bytes, default_context);
        ASSERT_TRUE(!bytes_status);
        Nested result{};
        const auto result_status = srl::deserialize(bytes.span(), result, default_context);
        ASSERT_TRUE(!result_status);
        EXPECT_EQ(result.name, "base");
        EXPECT_EQ(result.local.name, "field");
        EXPECT_EQ(result.record.second, 44);
        EXPECT_EQ(result.stream.first, 55u);
        ASSERT_EQ(result.items.size(), 2u);
        EXPECT_EQ(result.items[1].second, 100);
        srl::byte_buffer record{};
        const auto record_status = srl::serialize(Record{{12, "local"}, {34, 56}, {78, 90}}, record, default_context);
        ASSERT_TRUE(!record_status);
        Record decoded{};
        const auto decoded_status = srl::deserialize(record.span(), decoded, default_context);
        ASSERT_TRUE(!decoded_status);
        EXPECT_EQ(decoded.local.name, "local");
        EXPECT_EQ(decoded.overridden.second, 90);
        const auto path = std::filesystem::temp_directory_path() / "srl_auto_stream_test.bin";
        ASSERT_FALSE(srl::write_block(path, bytes.span()));
        auto block = srl::read_block(path);
        std::filesystem::remove(path);
        ASSERT_TRUE(block);
        Nested loaded{};
        ASSERT_FALSE(srl::deserialize(block->span(), loaded, default_context));
        EXPECT_EQ(loaded.local.name, "field");
    }

    TEST(AutoStream, ConversionRunsOnce) {
        Converted value;
        value.value.count = 42;
        Runtime::writes = 0;
        srl::byte_buffer bytes{};
        const auto bytes_status = srl::serialize(value, bytes, default_context);
        ASSERT_TRUE(!bytes_status);
        EXPECT_EQ(Runtime::writes, 1);
        Converted result{};
        const auto result_status = srl::deserialize(bytes.span(), result, default_context);
        ASSERT_TRUE(!result_status);
        EXPECT_EQ(result.value.count, 42u);
    }

    TEST(AutoStream, FieldModesAndIgnoredIds) {
        int marker{};
        srl::byte_buffer bytes{};
        const auto bytes_status = srl::serialize(ForcedLeaf{{12, 34}, &marker}, bytes, default_context);
        ASSERT_TRUE(!bytes_status);
        ForcedLeaf result{};
        const auto result_status = srl::deserialize(bytes.span(), result, default_context);
        ASSERT_TRUE(!result_status);
        EXPECT_EQ(result.value.second, 34);
        EXPECT_EQ(result.pointer, &marker);
        srl::byte_buffer overridden{};
        const auto overridden_status = srl::serialize(OverrideIds{{56, 78}}, overridden, default_context);
        ASSERT_TRUE(!overridden_status);
        EXPECT_EQ(overridden.size(), srl::serialized_size<OverrideIds>());
        OverrideIds ids{};
        const auto ids_status = srl::deserialize(overridden.span(), ids, default_context);
        ASSERT_TRUE(!ids_status);
        EXPECT_EQ(ids.value.first, 56u);
        EXPECT_EQ(ids.value.second, 78);
    }

    TEST(AutoStream, MalformedInputAndTransactionalDeserialize) {
        srl::byte_buffer bytes{};
        const auto bytes_status = srl::serialize(Numbers{12, 34}, bytes, default_context);
        ASSERT_TRUE(!bytes_status);
        const auto original = bytes;
        const auto root = open_document(bytes.span());
        const auto second = root.at(1).offset;
        bytes.resize(bytes.size() - 1);
        set_u64(bytes, root.root_offset(), bytes.size() - root.root_offset());
        Numbers out{77, 88};
        auto status = srl::deserialize(bytes.span(), out, default_context);
        EXPECT_EQ(status.code, srl::error_code::payload_out_of_bounds);
        EXPECT_EQ(status.offset, second);
        EXPECT_FALSE(status.id.has_value());
        EXPECT_EQ(out.first, 77u);
        bytes = original;
        bytes.push_back(std::byte{});
        set_u64(bytes, root.root_offset(), bytes.size() - root.root_offset());
        Numbers rejected_value_1{};
        require_error(srl::deserialize(bytes.span(), rejected_value_1, default_context),
                      srl::error_code::trailing_bytes, "stream trailing bytes");
        srl::byte_buffer nested{};
        const auto nested_status = srl::serialize(Converted{}, nested, default_context);
        ASSERT_TRUE(!nested_status);
        const auto nested_doc = open_document(nested.span());
        const auto child = nested_doc.at(0).offset;
        set_u64(nested, child, 0xffffffffu);
        Converted rejected_value_2{};
        require_error(srl::deserialize(nested.span(), rejected_value_2, default_context),
                      srl::error_code::payload_out_of_bounds, "oversized child");
        set_u64(nested, child, 0);
        Converted rejected_value_3{};
        require_error(srl::deserialize(nested.span(), rejected_value_3, default_context),
                      srl::error_code::invalid_entry_size, "undersized child");
        nested.resize(child + 4);
        set_u64(nested, nested_doc.root_offset(), nested.size() - nested_doc.root_offset());
        Converted rejected_value_4{};
        require_error(srl::deserialize(nested.span(), rejected_value_4, default_context),
                      srl::error_code::truncated_entry_header, "truncated child");
        srl::byte_buffer sequence{};
        const auto sequence_status = srl::serialize(Sequence{}, sequence, default_context);
        ASSERT_TRUE(!sequence_status);
        Numbers rejected_value_5{};
        require_error(srl::deserialize(sequence.span(), rejected_value_5, default_context),
                      srl::error_code::entry_kind_mismatch, "record accepted as stream");
    }
}
