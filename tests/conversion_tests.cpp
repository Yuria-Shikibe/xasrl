#include <memory_resource>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "srl/io.hpp"
#include "srl/serialization.hpp"

namespace conversion_tests {
    inline const xasrl::serialization_context default_context;

    using stored_integer = std::uint32_t;

    inline void check(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    template<typename Exception, typename Function>
    void expect_throw(Function &&function) {
        bool thrown = false;
        try {
            function();
        } catch (const Exception &) {
            thrown = true;
        }
        check(thrown, "expected exception was not propagated");
    }

    struct runtime_number {
        std::uint32_t value{};
        static inline int writes{};
        static inline int reads{};

        runtime_number() = default;
        explicit runtime_number(std::uint32_t value) : value(value) { ++reads; }
        explicit operator std::uint32_t() const { ++writes; return value; }
    };

    struct direct_record {
        [[=xasrl::tag::field{7}]]
        [[=xasrl::conversion_tag{^^stored_integer}]]
        runtime_number number;
    };

    struct stored_number {
        [[=xasrl::tag::field{1}]]
        std::uint32_t value{};

        stored_number() = default;
        explicit stored_number(const runtime_number &mem) : value(mem.value) {}
        explicit operator runtime_number() && { return runtime_number{value}; }
    };

    struct record_conversion {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^stored_number}]]
        runtime_number number;
    };

    struct move_only_storage {
        [[=xasrl::tag::field{1}]]
        std::uint32_t value{};

        move_only_storage() = default;
        move_only_storage(const move_only_storage &) = delete;
        move_only_storage(move_only_storage &&) = default;
        move_only_storage &operator=(move_only_storage &&) = default;
        explicit move_only_storage(const runtime_number &mem) : value(mem.value) {}
        explicit operator runtime_number() && { return runtime_number{value}; }
    };

    struct move_only_conversion {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^move_only_storage}]]
        runtime_number number;
    };

    struct object { int runtime_value{}; };
    struct object_key {};
    inline constexpr unsigned char alternate_key{};

    struct reference_record {
        [[=xasrl::tag::field{7}]]
        [[=xasrl::conversion_tag{^^std::string, xasrl::handler_key<object_key>()}]]
        object *reference{};
    };

    struct wire_reference {
        [[=xasrl::tag::field{7}]]
        std::string id;
    };

    struct two_references {
        [[=xasrl::tag::field{7}]]
        [[=xasrl::conversion_tag{^^std::string, xasrl::handler_key<object_key>()}]]
        object *first{};
        [[=xasrl::tag::field{9}]]
        [[=xasrl::conversion_tag{^^std::string, &alternate_key}]]
        object *second{};
    };

    struct reference_handler : xasrl::typed_reference_handler<object *, std::string> {
        object &target;
        std::string id;
        int writes{};
        int reads{};
        bool throw_on_read{};

        reference_handler(object &target, std::string id) : target(target), id(std::move(id)) {}

        xasrl::error to_storage(object *const &mem, std::string &srl) override {
            ++writes;
            if (mem == nullptr) {
                srl.clear();
            } else if (mem == &target) {
                srl = id;
            } else {
                return xasrl::detail::fail(xasrl::error_code::invalid_reference);
            }
            return {};
        }

        xasrl::error from_storage(object *&mem, const std::string &srl) override {
            ++reads;
            if (srl.empty()) {
                mem = nullptr;
            } else if (srl == id) {
                mem = &target;
            } else {
                return xasrl::detail::fail(xasrl::error_code::unresolved_reference);
            }
            if (throw_on_read) {
                throw std::runtime_error("external service failure");
            }
            return {};
        }
    };

    struct raw_reference_handler : xasrl::context_reference_handler {
        object &target;

        explicit raw_reference_handler(object &target)
            : context_reference_handler(std::in_place_type<object *>, std::in_place_type<std::string>),
              target(target) {}

        xasrl::error mem2srl(const void *mem, void *srl) override {
            if (*static_cast<object *const *>(mem) != &target) {
                return xasrl::detail::fail(xasrl::error_code::invalid_reference);
            }
            *static_cast<std::string *>(srl) = "raw-id";
            return {};
        }

        xasrl::error srl2mem(void *mem, const void *srl) override {
            if (*static_cast<const std::string *>(srl) != "raw-id") {
                return xasrl::detail::fail(xasrl::error_code::unresolved_reference);
            }
            *static_cast<object **>(mem) = &target;
            return {};
        }
    };

    struct wrong_handler : xasrl::typed_reference_handler<int, std::string> {
        xasrl::error to_storage(const int &, std::string &) override { return {}; }
        xasrl::error from_storage(int &, const std::string &) override { return {}; }
    };

    struct nested_record {
        [[=xasrl::tag::field{2}]]
        std::vector<reference_record> references;
        [[=xasrl::tag::field{1}]]
        std::map<int, reference_record> lookup;
    };

    struct reference_bundle {
        std::vector<reference_record> values;
        static inline int writes{};
        reference_bundle() = default;
        explicit reference_bundle(std::vector<reference_record> values) : values(std::move(values)) {}
        explicit operator std::vector<reference_record>() const { ++writes; return values; }
    };

    struct converted_nested_record {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^std::vector<reference_record>}]]
        reference_bundle bundle;
    };

    struct runtime_set {
        std::set<int> values;
        runtime_set() = default;
        explicit runtime_set(std::set<int> values) : values(std::move(values)) {}
        explicit operator std::set<int>() const { return values; }
    };

    struct converted_container {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^std::set<int>}]]
        [[=xasrl::tag::container{
            .duplicates = xasrl::duplicate_policy::reject}]]
        runtime_set values;
    };

    struct wire_container {
        [[=xasrl::tag::field{1}]]
        std::vector<int> values;
    };

    struct missing_field_tag {
        [[=xasrl::tag::field{1}]] int existing{};
        [[=xasrl::conversion_tag{^^int}]] int value{};
    };
    struct duplicate_conversion {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^int}]]
        [[=xasrl::conversion_tag{^^int}]] int value{};
    };
    struct leaf_conflict {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::tag::leaf{}]]
        [[=xasrl::conversion_tag{^^int}]] int value{};
    };
    struct [[=xasrl::tag::leaf{}]] leaf_conversion {
        [[=xasrl::conversion_tag{^^int}]] int value{};
    };
    struct const_conversion {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^int}]] const int value{};
    };
    struct volatile_conversion {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^int}]] volatile int value{};
    };
    struct reference_conversion {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^int}]] int &value;
    };
    struct bit_conversion {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^unsigned}]] unsigned value : 1;
    };
    struct invalid_storage {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^void, xasrl::handler_key<object_key>()}]] int value{};
    };
    struct non_type_storage {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^object::runtime_value}]] int value{};
    };
    struct pointer_storage {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^int *, xasrl::handler_key<object_key>()}]] int value{};
    };
    struct no_conversion {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^std::string}]] int value{};
    };
    struct cyclic_conversion {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^cyclic_conversion, xasrl::handler_key<object_key>()}]]
        cyclic_conversion *value{};
    };
    struct cyclic_container_conversion {
        [[=xasrl::tag::field{1}]]
        [[=xasrl::conversion_tag{^^std::vector<cyclic_container_conversion>, xasrl::handler_key<object_key>()}]]
        cyclic_container_conversion *value{};
    };

    template<typename T>
    concept serializes_without_context = requires(const T &value, xasrl::byte_buffer &bytes) {
        xasrl::serialize(value, bytes);
    };

    template<typename T>
    concept serializes_with_context = requires(const T &value, xasrl::byte_buffer &bytes,
                                          const xasrl::serialization_context &context) {
        xasrl::serialize(value, bytes, context);
    };

    static_assert(xasrl::schema_valid(^^direct_record));
    static_assert(xasrl::schema_valid(^^record_conversion));
    static_assert(xasrl::schema_valid(^^move_only_conversion));
    static_assert(xasrl::schema_valid(^^reference_record));
    static_assert(xasrl::schema_valid(^^two_references));
    static_assert(xasrl::schema_valid(^^converted_container));
    static_assert(xasrl::schema_valid(^^converted_nested_record));
    static_assert(!serializes_without_context<direct_record>);
    static_assert(serializes_with_context<direct_record>);
    static_assert(requires(std::span<const std::byte> bytes, direct_record &out) {
        xasrl::deserialize(bytes, out, default_context);
    });
    static_assert(!xasrl::schema_valid(^^missing_field_tag));
    static_assert(!xasrl::schema_valid(^^duplicate_conversion));
    static_assert(!xasrl::schema_valid(^^leaf_conflict));
    static_assert(!xasrl::schema_valid(^^leaf_conversion));
    static_assert(!xasrl::schema_valid(^^const_conversion));
    static_assert(!xasrl::schema_valid(^^volatile_conversion));
    static_assert(!xasrl::schema_valid(^^reference_conversion));
    static_assert(!xasrl::schema_valid(^^bit_conversion));
    static_assert(!xasrl::schema_valid(^^invalid_storage));
    static_assert(!xasrl::schema_valid(^^non_type_storage));
    static_assert(!xasrl::schema_valid(^^pointer_storage));
    static_assert(!xasrl::schema_valid(^^no_conversion));
    static_assert(!xasrl::schema_valid(^^cyclic_conversion));
    static_assert(!xasrl::schema_valid(^^cyclic_container_conversion));
    static_assert(xasrl::handler_key<object_key>() == xasrl::handler_key<const object_key &>());
    static_assert(xasrl::handler_key<object_key>() != &alternate_key);

    inline void test_direct() {
        const direct_record source{runtime_number{42}};
        runtime_number::writes = 0;
        xasrl::byte_buffer bytes{};
        const auto bytes_status = xasrl::serialize(source, bytes, default_context);
        check(!bytes_status && runtime_number::writes == 1, "direct conversion repeated during pack");
        runtime_number::reads = 0;
        direct_record copy{};
        const auto copy_status = xasrl::deserialize(bytes.span(), copy, default_context);
        check(!copy_status && copy.number.value == 42 && runtime_number::reads == 1,
              "direct conversion did not round trip");
        runtime_number::writes = 0;
        auto size = xasrl::serialized_size(source, default_context);
        check(size == bytes.size() && runtime_number::writes == 0,
              "direct conversion measurement failed");
        xasrl::byte_buffer destination;
        destination.resize(size);
        runtime_number::writes = 0;
        check(!xasrl::serialize(source, destination.span(), default_context)
              && destination == bytes && runtime_number::writes == 1, "direct span serialization repeated conversion");

        xasrl::byte_buffer nested{};
        const auto nested_status = xasrl::serialize(record_conversion{runtime_number{73}}, nested, default_context);
        check(!nested_status, "record storage conversion failed");
        record_conversion nested_copy{};
        const auto nested_copy_status = xasrl::deserialize(nested.span(), nested_copy, default_context);
        check(!nested_copy_status && nested_copy.number.value == 73, "record storage decode failed");

        const std::vector<move_only_conversion> move_only{
            {runtime_number{1}}, {runtime_number{2}}, {runtime_number{3}}};
        xasrl::byte_buffer moved{};
        xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
        const auto moved_status = xasrl::serialize(move_only, moved, xasrl::srl_context{default_context, recorder});
        check(!moved_status, "move-only storage could not be prepared");
        std::vector<move_only_conversion> moved_copy{};
        const auto moved_copy_status = xasrl::deserialize(moved.span(), moved_copy, default_context);
        check(!moved_copy_status && moved_copy.size() == 3
              && moved_copy.at(2).number.value == 3, "move-only storage did not round trip");

        auto malformed = bytes;
        malformed.resize(malformed.size() - 1);
        constexpr auto root = xasrl::detail::document_header_size;
        constexpr auto leaf_width = root + xasrl::detail::entry_head_wire_size + xasrl::detail::definition_reference_size
                                    + xasrl::detail::definition_header_size + 8;
        xasrl::detail::write_trivial(malformed.span().subspan(root), static_cast<xasrl::szt>(malformed.size() - root));
        xasrl::detail::write_trivial(malformed.span().subspan(leaf_width), xasrl::szt{3});
        direct_record unchanged{runtime_number{99}};
        runtime_number::reads = 0;
        auto rejected_number = xasrl::deserialize(malformed.span(), unchanged, default_context);
        check(rejected_number && rejected_number.code == xasrl::error_code::invalid_payload_shape
              && rejected_number.id == 7 && runtime_number::reads == 0
              && unchanged.number.value == 99, "invalid storage reached direct conversion");

        xasrl::byte_buffer container{};
        const auto container_status = xasrl::serialize(converted_container{runtime_set{{1, 2, 3}}}, container, default_context);
        check(!container_status, "container conversion failed");
        converted_container container_copy{};
        const auto container_copy_status = xasrl::deserialize(container.span(), container_copy, default_context);
        check(!container_copy_status && container_copy.values.values == std::set{1, 2, 3},
              "container conversion decode failed");
        xasrl::byte_buffer duplicates{};
        const auto duplicates_status = xasrl::serialize(wire_container{{1, 1}}, duplicates, default_context);
        check(!duplicates_status, "duplicate fixture failed");
        converted_container rejected{};
        const auto rejected_status = xasrl::deserialize(duplicates.span(), rejected, default_context);
        check(rejected_status && rejected_status.code == xasrl::error_code::duplicate_container_element,
              "converted container lost duplicate policy");
    }

    inline void test_context() {
        object first{1};
        object second{2};
        reference_handler writer{first, "asset:stable-id"};
        reference_handler reader{second, "asset:stable-id"};
        xasrl::serialization_context write_context;
        xasrl::serialization_context read_context;
        write_context.register_handler(xasrl::handler_key<object_key>(), writer);
        read_context.register_handler(xasrl::handler_key<object_key>(), reader);
        const reference_record source{&first};
        xasrl::byte_buffer bytes{};
        const auto bytes_status = xasrl::serialize(source, bytes, write_context);
        check(!bytes_status && writer.writes == 1, "context pack repeated conversion");
        reference_record copy{};
        const auto copy_status = xasrl::deserialize(bytes.span(), copy, read_context);
        check(!copy_status && copy.reference == &second && reader.reads == 1,
              "reference was not relocated through the receiving context");
        xasrl::byte_buffer relocated{};
        const auto relocated_status = xasrl::serialize(copy, relocated, read_context);
        check(!relocated_status && relocated == bytes, "runtime address leaked into stored bytes");
        wire_reference stored{};
        const auto stored_status = xasrl::deserialize(bytes.span(), stored, default_context);
        check(!stored_status && stored.id == writer.id, "storage payload is not SrlTy");

        writer.writes = 0;
        auto size = xasrl::serialized_size(source, write_context);
        check(size.has_value() && *size == bytes.size() && writer.writes == 1,
              "context measurement repeated conversion");
        xasrl::byte_buffer destination;
        destination.resize(*size);
        writer.writes = 0;
        check(!xasrl::serialize(source, destination.span(), write_context)
              && destination == bytes && writer.writes == 1, "context span serialization repeated conversion");
        destination.resize(1);
        auto wrong_size = xasrl::serialize(source, destination.span(), write_context);
        check(wrong_size && wrong_size.code == xasrl::error_code::invalid_entry_size,
              "span serialization accepted wrong destination size");

        xasrl::byte_buffer null_bytes{};
        const auto null_bytes_status = xasrl::serialize(reference_record{}, null_bytes, write_context);
        check(!null_bytes_status, "null reference encoding failed");
        reference_record null_copy{};
        const auto null_copy_status = xasrl::deserialize(null_bytes.span(), null_copy, read_context);
        check(!null_copy_status && null_copy.reference == nullptr, "legal null reference was rejected");
        xasrl::byte_buffer invalid{};
        const auto invalid_status = xasrl::serialize(reference_record{&second}, invalid, write_context);
        check(invalid_status && invalid_status.code == xasrl::error_code::invalid_reference
              && invalid_status.id == 7, "invalid outgoing reference was accepted");

        xasrl::byte_buffer unknown{};
        const auto unknown_status = xasrl::serialize(wire_reference{"unknown-id"}, unknown, default_context);
        check(!unknown_status, "unknown reference fixture failed");
        reference_record unchanged{&first};
        auto failed = xasrl::deserialize(unknown.span(), unchanged, read_context);
        check(failed && failed.code == xasrl::error_code::unresolved_reference
              && failed.id == 7
              && failed.offset == 60
              && unchanged.reference == &first,
              "unknown reference lost error context or changed output");
        reader.throw_on_read = true;
        expect_throw<std::runtime_error>([&] { (void)xasrl::deserialize(bytes.span(), unchanged, read_context); });
        check(unchanged.reference == &first, "handler exception changed unpack output");
        reader.throw_on_read = false;

        expect_throw<std::logic_error>([&] { (void)xasrl::serialize(source, bytes, xasrl::serialization_context{}); });
        expect_throw<std::logic_error>([&] {
            (void)xasrl::deserialize(bytes.span(), unchanged, xasrl::serialization_context{});
        });
        expect_throw<std::logic_error>([&] {
            write_context.register_handler(xasrl::handler_key<object_key>(), reader);
        });
        expect_throw<std::invalid_argument>([&] { write_context.register_handler(nullptr, reader); });
        wrong_handler wrong;
        xasrl::serialization_context mismatched;
        mismatched.register_handler(xasrl::handler_key<object_key>(), wrong);
        expect_throw<std::logic_error>([&] { (void)xasrl::serialize(source, bytes, mismatched); });
        expect_throw<std::logic_error>([&] { (void)xasrl::deserialize(bytes.span(), unchanged, mismatched); });

        reference_handler alternate{second, "alternate-id"};
        write_context.register_handler(&alternate_key, alternate);
        xasrl::byte_buffer dual{};
        const auto dual_status = xasrl::serialize(two_references{&first, &second}, dual, write_context);
        check(!dual_status, "two strategies for the same type pair failed");
        two_references dual_copy{};
        const auto dual_copy_status = xasrl::deserialize(dual.span(), dual_copy, write_context);
        check(!dual_copy_status && dual_copy.first == &first && dual_copy.second == &second,
              "handler identity was replaced by type-pair identity");

        nested_record nested{{reference_record{&first}, reference_record{}}, {{3, {&first}}}};
        writer.writes = 0;
        xasrl::byte_buffer nested_bytes{};
        xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
        const auto nested_bytes_status = xasrl::serialize(nested, nested_bytes, xasrl::srl_context{write_context, recorder});
        check(!nested_bytes_status && writer.writes == 3, "nested conversion repeated or omitted");
        nested_record nested_copy{};
        const auto nested_copy_status = xasrl::deserialize(nested_bytes.span(), nested_copy, read_context);
        check(!nested_copy_status && nested_copy.references[0].reference == &second
              && nested_copy.references[1].reference == nullptr
              && nested_copy.lookup.at(3).reference == &second, "nested context was not forwarded");

        writer.writes = 0;
        xasrl::byte_buffer nested_destination;
        nested_destination.resize(nested_bytes.size());
        check(!xasrl::serialize(nested, nested_destination.span(), xasrl::srl_context{write_context, recorder})
              && writer.writes == 3 && nested_destination == nested_bytes,
              "nested span serialization repeated conversion");

        raw_reference_handler raw{second};
        xasrl::serialization_context raw_context;
        raw_context.register_handler(xasrl::handler_key<object_key>(), raw);
        xasrl::byte_buffer raw_bytes{};
        const auto raw_bytes_status = xasrl::serialize(reference_record{&second}, raw_bytes, raw_context);
        check(!raw_bytes_status, "raw erased handler encoding failed");
        reference_record raw_copy{};
        const auto raw_copy_status = xasrl::deserialize(raw_bytes.span(), raw_copy, raw_context);
        check(!raw_copy_status && raw_copy.reference == &second, "raw erased handler decoding failed");

        converted_nested_record converted{reference_bundle{{reference_record{&first}}}};
        reference_bundle::writes = 0;
        writer.writes = 0;
        xasrl::byte_buffer converted_bytes{};
        const auto converted_bytes_status = xasrl::serialize(converted, converted_bytes, xasrl::srl_context{write_context, recorder});
        check(!converted_bytes_status && writer.writes == 1 && reference_bundle::writes == 1,
              "conversion within SrlTy repeated or lost context");
        converted_nested_record converted_copy{};
        const auto converted_copy_status = xasrl::deserialize(converted_bytes.span(), converted_copy, read_context);
        check(!converted_copy_status && converted_copy.bundle.values[0].reference == &second,
              "conversion within SrlTy failed to decode");

        reference_record array[] = {{&first}, {nullptr}};
        xasrl::byte_buffer array_bytes{};
        const auto array_bytes_status = xasrl::serialize(array, array_bytes, xasrl::srl_context{write_context, recorder});
        check(!array_bytes_status, "context root array pack failed");
        reference_record array_copy[2]{};
        check(!xasrl::deserialize(array_bytes.span(), array_copy, read_context)
              && array_copy[0].reference == &second && array_copy[1].reference == nullptr,
              "context root array unpack failed");

        const auto path = std::filesystem::temp_directory_path() / "srl_conversion_round_trip.bin";
        check(!xasrl::write_block(path, bytes.span()), "context block write failed");
        auto block = xasrl::read_block(path);
        check(block.has_value(), "context block read failed");
        reference_record loaded{};
        check(!xasrl::deserialize(block->span(), loaded, read_context)
              && loaded.reference == &second, "context file decode failed");
        std::filesystem::remove(path);
    }

    TEST(Conversion, DirectStorage) {
        test_direct();
    }

    TEST(Conversion, SerializationContext) {
        test_context();
    }
}
