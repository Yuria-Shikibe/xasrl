#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "srl/io.hpp"

namespace conversion_tests {
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
        [[=srl::tag::field{7}]]
        [[=srl::conversion_tag{^^stored_integer}]]
        runtime_number number;
    };

    struct stored_number {
        [[=srl::tag::field{1}]]
        std::uint32_t value{};

        stored_number() = default;
        explicit stored_number(const runtime_number &mem) : value(mem.value) {}
        explicit operator runtime_number() && { return runtime_number{value}; }
    };

    struct record_conversion {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^stored_number}]]
        runtime_number number;
    };

    struct move_only_storage {
        [[=srl::tag::field{1}]]
        std::uint32_t value{};

        move_only_storage() = default;
        move_only_storage(const move_only_storage &) = delete;
        move_only_storage(move_only_storage &&) = default;
        move_only_storage &operator=(move_only_storage &&) = default;
        explicit move_only_storage(const runtime_number &mem) : value(mem.value) {}
        explicit operator runtime_number() && { return runtime_number{value}; }
    };

    struct move_only_conversion {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^move_only_storage}]]
        runtime_number number;
    };

    struct object { int runtime_value{}; };
    struct object_key {};
    inline constexpr unsigned char alternate_key{};

    struct reference_record {
        [[=srl::tag::field{7}]]
        [[=srl::conversion_tag{^^std::string, srl::handler_key<object_key>()}]]
        object *reference{};
    };

    struct wire_reference {
        [[=srl::tag::field{7}]]
        std::string id;
    };

    struct two_references {
        [[=srl::tag::field{7}]]
        [[=srl::conversion_tag{^^std::string, srl::handler_key<object_key>()}]]
        object *first{};
        [[=srl::tag::field{9}]]
        [[=srl::conversion_tag{^^std::string, &alternate_key}]]
        object *second{};
    };

    struct reference_handler : srl::typed_reference_handler<object *, std::string> {
        object &target;
        std::string id;
        int writes{};
        int reads{};
        bool throw_on_read{};

        reference_handler(object &target, std::string id) : target(target), id(std::move(id)) {}

        std::expected<void, srl::error> to_storage(object *const &mem, std::string &srl) override {
            ++writes;
            if (mem == nullptr) {
                srl.clear();
            } else if (mem == &target) {
                srl = id;
            } else {
                return srl::detail::fail(srl::error_code::invalid_reference);
            }
            return {};
        }

        std::expected<void, srl::error> from_storage(object *&mem, const std::string &srl) override {
            ++reads;
            if (srl.empty()) {
                mem = nullptr;
            } else if (srl == id) {
                mem = &target;
            } else {
                return srl::detail::fail(srl::error_code::unresolved_reference);
            }
            if (throw_on_read) {
                throw std::runtime_error("external service failure");
            }
            return {};
        }
    };

    struct raw_reference_handler : srl::context_reference_handler {
        object &target;

        explicit raw_reference_handler(object &target)
            : context_reference_handler(std::in_place_type<object *>, std::in_place_type<std::string>),
              target(target) {}

        std::expected<void, srl::error> mem2srl(const void *mem, void *srl) override {
            if (*static_cast<object *const *>(mem) != &target) {
                return srl::detail::fail(srl::error_code::invalid_reference);
            }
            *static_cast<std::string *>(srl) = "raw-id";
            return {};
        }

        std::expected<void, srl::error> srl2mem(void *mem, const void *srl) override {
            if (*static_cast<const std::string *>(srl) != "raw-id") {
                return srl::detail::fail(srl::error_code::unresolved_reference);
            }
            *static_cast<object **>(mem) = &target;
            return {};
        }
    };

    struct wrong_handler : srl::typed_reference_handler<int, std::string> {
        std::expected<void, srl::error> to_storage(const int &, std::string &) override { return {}; }
        std::expected<void, srl::error> from_storage(int &, const std::string &) override { return {}; }
    };

    struct nested_record {
        [[=srl::tag::field{2}]]
        std::vector<reference_record> references;
        [[=srl::tag::field{1}]]
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
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^std::vector<reference_record>}]]
        reference_bundle bundle;
    };

    struct runtime_set {
        std::set<int> values;
        runtime_set() = default;
        explicit runtime_set(std::set<int> values) : values(std::move(values)) {}
        explicit operator std::set<int>() const { return values; }
    };

    struct converted_container {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^std::set<int>}]]
        [[=srl::tag::container{.duplicates = srl::duplicate_policy::reject}]]
        runtime_set values;
    };

    struct wire_container {
        [[=srl::tag::field{1}]]
        std::vector<int> values;
    };

    struct missing_field_tag {
        [[=srl::tag::field{1}]] int existing{};
        [[=srl::conversion_tag{^^int}]] int value{};
    };
    struct duplicate_conversion {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^int}]]
        [[=srl::conversion_tag{^^int}]] int value{};
    };
    struct atomic_conflict {
        [[=srl::tag::field{1}]]
        [[=srl::tag::atomic{}]]
        [[=srl::conversion_tag{^^int}]] int value{};
    };
    struct [[=srl::tag::atomic{}]] atomic_conversion {
        [[=srl::conversion_tag{^^int}]] int value{};
    };
    struct const_conversion {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^int}]] const int value{};
    };
    struct volatile_conversion {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^int}]] volatile int value{};
    };
    struct reference_conversion {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^int}]] int &value;
    };
    struct bit_conversion {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^unsigned}]] unsigned value : 1;
    };
    struct invalid_storage {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^void, srl::handler_key<object_key>()}]] int value{};
    };
    struct non_type_storage {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^object::runtime_value}]] int value{};
    };
    struct pointer_storage {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^int *, srl::handler_key<object_key>()}]] int value{};
    };
    struct no_conversion {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^std::string}]] int value{};
    };
    struct cyclic_conversion {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^cyclic_conversion, srl::handler_key<object_key>()}]]
        cyclic_conversion *value{};
    };
    struct cyclic_container_conversion {
        [[=srl::tag::field{1}]]
        [[=srl::conversion_tag{^^std::vector<cyclic_container_conversion>, srl::handler_key<object_key>()}]]
        cyclic_container_conversion *value{};
    };

    template<typename T>
    concept packs_without_context = requires(const T &value) { srl::pack(value); };

    static_assert(srl::schema_valid<direct_record>());
    static_assert(srl::schema_valid<record_conversion>());
    static_assert(srl::schema_valid<move_only_conversion>());
    static_assert(srl::schema_valid<reference_record>());
    static_assert(srl::schema_valid<two_references>());
    static_assert(srl::schema_valid<converted_container>());
    static_assert(srl::schema_valid<converted_nested_record>());
    static_assert(!srl::requires_context<direct_record>());
    static_assert(srl::requires_context<nested_record>());
    static_assert(srl::requires_context<converted_nested_record>());
    static_assert(packs_without_context<direct_record>);
    static_assert(!packs_without_context<reference_record>);
    static_assert(!packs_without_context<nested_record>);
    static_assert(requires(std::span<const std::byte> bytes, direct_record &out,
                           const std::filesystem::path &path) {
        srl::extract<direct_record>(bytes, {});
        srl::unpack(bytes, out, {});
        srl::decode_root(bytes, out, {});
        srl::load<direct_record>(path, {});
    });
    static_assert(!srl::schema_valid<missing_field_tag>());
    static_assert(!srl::schema_valid<duplicate_conversion>());
    static_assert(!srl::schema_valid<atomic_conflict>());
    static_assert(!srl::schema_valid<atomic_conversion>());
    static_assert(!srl::schema_valid<const_conversion>());
    static_assert(!srl::schema_valid<volatile_conversion>());
    static_assert(!srl::schema_valid<reference_conversion>());
    static_assert(!srl::schema_valid<bit_conversion>());
    static_assert(!srl::schema_valid<invalid_storage>());
    static_assert(!srl::schema_valid<non_type_storage>());
    static_assert(!srl::schema_valid<pointer_storage>());
    static_assert(!srl::schema_valid<no_conversion>());
    static_assert(!srl::schema_valid<cyclic_conversion>());
    static_assert(!srl::schema_valid<cyclic_container_conversion>());
    static_assert(srl::handler_key<object_key>() == srl::handler_key<const object_key &>());
    static_assert(srl::handler_key<object_key>() != &alternate_key);

    inline void test_direct() {
        const direct_record source{runtime_number{42}};
        runtime_number::writes = 0;
        auto bytes = srl::pack(source);
        check(bytes.has_value() && runtime_number::writes == 1, "direct conversion repeated during pack");
        runtime_number::reads = 0;
        auto copy = srl::extract<direct_record>(bytes->span());
        check(copy.has_value() && copy->number.value == 42 && runtime_number::reads == 1,
              "direct conversion did not round trip");
        runtime_number::writes = 0;
        auto size = srl::serialized_size(source);
        check(size.has_value() && *size == bytes->size() && runtime_number::writes == 1,
              "direct conversion measurement failed");
        srl::byte_buffer destination;
        destination.resize(*size);
        runtime_number::writes = 0;
        check(srl::pack_into(destination.span(), source).has_value()
              && destination == *bytes && runtime_number::writes == 1, "direct pack_into repeated conversion");

        auto nested = srl::pack(record_conversion{runtime_number{73}});
        check(nested.has_value(), "record storage conversion failed");
        auto nested_copy = srl::extract<record_conversion>(nested->span());
        check(nested_copy.has_value() && nested_copy->number.value == 73, "record storage decode failed");

        const std::vector<move_only_conversion> move_only{
            {runtime_number{1}}, {runtime_number{2}}, {runtime_number{3}}};
        auto moved = srl::pack(move_only);
        check(moved.has_value(), "move-only storage could not be prepared");
        auto moved_copy = srl::extract<std::vector<move_only_conversion>>(moved->span());
        check(moved_copy.has_value() && moved_copy->size() == 3
              && moved_copy->at(2).number.value == 3, "move-only storage did not round trip");

        auto malformed = *bytes;
        malformed.resize(malformed.size() - 1);
        srl::detail::write_trivial(malformed.span(), static_cast<std::uint32_t>(malformed.size()));
        direct_record unchanged{runtime_number{99}};
        runtime_number::reads = 0;
        auto rejected_number = srl::unpack(malformed.span(), unchanged);
        check(!rejected_number && rejected_number.error().code == srl::error_code::invalid_payload_shape
              && rejected_number.error().id == 7 && runtime_number::reads == 0
              && unchanged.number.value == 99, "invalid storage reached direct conversion");

        auto container = srl::pack(converted_container{runtime_set{{1, 2, 3}}});
        check(container.has_value(), "container conversion failed");
        auto container_copy = srl::extract<converted_container>(container->span());
        check(container_copy.has_value() && container_copy->values.values == std::set{1, 2, 3},
              "container conversion decode failed");
        auto duplicates = srl::pack(wire_container{{1, 1}});
        check(duplicates.has_value(), "duplicate fixture failed");
        auto rejected = srl::extract<converted_container>(duplicates->span());
        check(!rejected && rejected.error().code == srl::error_code::duplicate_container_element,
              "converted container lost duplicate policy");
    }

    inline void test_context() {
        object first{1};
        object second{2};
        reference_handler writer{first, "asset:stable-id"};
        reference_handler reader{second, "asset:stable-id"};
        srl::serialization_context write_context;
        srl::serialization_context read_context;
        write_context.register_handler(srl::handler_key<object_key>(), writer);
        read_context.register_handler(srl::handler_key<object_key>(), reader);
        const reference_record source{&first};
        auto bytes = srl::pack(source, write_context);
        check(bytes.has_value() && writer.writes == 1, "context pack repeated conversion");
        auto copy = srl::extract<reference_record>(bytes->span(), read_context);
        check(copy.has_value() && copy->reference == &second && reader.reads == 1,
              "reference was not relocated through the receiving context");
        auto relocated = srl::pack(*copy, read_context);
        check(relocated.has_value() && *relocated == *bytes, "runtime address leaked into stored bytes");
        auto stored = srl::extract<wire_reference>(bytes->span());
        check(stored.has_value() && stored->id == writer.id, "storage payload is not SrlTy");

        writer.writes = 0;
        auto size = srl::serialized_size(source, write_context);
        check(size.has_value() && *size == bytes->size() && writer.writes == 1,
              "context measurement repeated conversion");
        srl::byte_buffer destination;
        destination.resize(*size);
        writer.writes = 0;
        check(srl::pack_into(destination.span(), source, write_context).has_value()
              && destination == *bytes && writer.writes == 1, "context pack_into repeated conversion");
        destination.resize(1);
        auto wrong_size = srl::pack_into(destination.span(), source, write_context);
        check(!wrong_size && wrong_size.error().code == srl::error_code::invalid_entry_size,
              "pack_into accepted wrong destination size");

        auto null_bytes = srl::pack(reference_record{}, write_context);
        check(null_bytes.has_value(), "null reference encoding failed");
        auto null_copy = srl::extract<reference_record>(null_bytes->span(), read_context);
        check(null_copy.has_value() && null_copy->reference == nullptr, "legal null reference was rejected");
        auto invalid = srl::pack(reference_record{&second}, write_context);
        check(!invalid && invalid.error().code == srl::error_code::invalid_reference
              && invalid.error().id == 7, "invalid outgoing reference was accepted");

        auto unknown = srl::pack(wire_reference{"unknown-id"});
        check(unknown.has_value(), "unknown reference fixture failed");
        reference_record unchanged{&first};
        auto failed = srl::unpack(unknown->span(), unchanged, read_context);
        check(!failed && failed.error().code == srl::error_code::unresolved_reference
              && failed.error().id == 7 && failed.error().offset == 24 && unchanged.reference == &first,
              "unknown reference lost error context or changed output");
        reader.throw_on_read = true;
        expect_throw<std::runtime_error>([&] { (void)srl::unpack(bytes->span(), unchanged, read_context); });
        check(unchanged.reference == &first, "handler exception changed unpack output");
        reader.throw_on_read = false;

        expect_throw<std::logic_error>([&] { (void)srl::pack(source, srl::serialization_context{}); });
        expect_throw<std::logic_error>([&] {
            (void)srl::extract<reference_record>(bytes->span(), srl::serialization_context{});
        });
        expect_throw<std::logic_error>([&] {
            write_context.register_handler(srl::handler_key<object_key>(), reader);
        });
        expect_throw<std::invalid_argument>([&] { write_context.register_handler(nullptr, reader); });
        wrong_handler wrong;
        srl::serialization_context mismatched;
        mismatched.register_handler(srl::handler_key<object_key>(), wrong);
        expect_throw<std::logic_error>([&] { (void)srl::pack(source, mismatched); });
        expect_throw<std::logic_error>([&] { (void)srl::extract<reference_record>(bytes->span(), mismatched); });

        reference_handler alternate{second, "alternate-id"};
        write_context.register_handler(&alternate_key, alternate);
        auto dual = srl::pack(two_references{&first, &second}, write_context);
        check(dual.has_value(), "two strategies for the same type pair failed");
        auto dual_copy = srl::extract<two_references>(dual->span(), write_context);
        check(dual_copy.has_value() && dual_copy->first == &first && dual_copy->second == &second,
              "handler identity was replaced by type-pair identity");

        nested_record nested{{reference_record{&first}, reference_record{}}, {{3, {&first}}}};
        writer.writes = 0;
        auto nested_bytes = srl::pack(nested, write_context);
        check(nested_bytes.has_value() && writer.writes == 3, "nested conversion repeated or omitted");
        auto nested_copy = srl::extract<nested_record>(nested_bytes->span(), read_context);
        check(nested_copy.has_value() && nested_copy->references[0].reference == &second
              && nested_copy->references[1].reference == nullptr
              && nested_copy->lookup.at(3).reference == &second, "nested context was not forwarded");

        writer.writes = 0;
        srl::byte_buffer nested_destination;
        nested_destination.resize(nested_bytes->size());
        check(srl::pack_into(nested_destination.span(), nested, write_context).has_value()
              && writer.writes == 3 && nested_destination == *nested_bytes,
              "nested pack_into repeated conversion");

        raw_reference_handler raw{second};
        srl::serialization_context raw_context;
        raw_context.register_handler(srl::handler_key<object_key>(), raw);
        auto raw_bytes = srl::pack(reference_record{&second}, raw_context);
        check(raw_bytes.has_value(), "raw erased handler encoding failed");
        auto raw_copy = srl::extract<reference_record>(raw_bytes->span(), raw_context);
        check(raw_copy.has_value() && raw_copy->reference == &second, "raw erased handler decoding failed");

        converted_nested_record converted{reference_bundle{{reference_record{&first}}}};
        reference_bundle::writes = 0;
        writer.writes = 0;
        auto converted_bytes = srl::pack(converted, write_context);
        check(converted_bytes.has_value() && writer.writes == 1 && reference_bundle::writes == 1,
              "conversion within SrlTy repeated or lost context");
        auto converted_copy = srl::extract<converted_nested_record>(converted_bytes->span(), read_context);
        check(converted_copy.has_value() && converted_copy->bundle.values[0].reference == &second,
              "conversion within SrlTy failed to decode");

        reference_record array[] = {{&first}, {nullptr}};
        auto array_bytes = srl::pack(array, write_context);
        check(array_bytes.has_value(), "context root array pack failed");
        reference_record array_copy[2]{};
        check(srl::unpack(array_bytes->span(), array_copy, read_context).has_value()
              && array_copy[0].reference == &second && array_copy[1].reference == nullptr,
              "context root array unpack failed");

        const auto path = std::filesystem::temp_directory_path() / "srl_conversion_round_trip.bin";
        check(srl::save(path, source, write_context).has_value(), "context save failed");
        auto loaded = srl::load<reference_record>(path, read_context);
        check(loaded.has_value() && loaded->reference == &second, "context load failed");
        std::filesystem::remove(path);
    }

    inline void run() {
        test_direct();
        test_context();
    }
}
