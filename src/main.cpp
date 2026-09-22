#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <expected>
#include <filesystem>
#include <fstream>
#include <forward_list>
#include <list>
#include <limits>
#include <map>
#include <print>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "srl/io.hpp"
#include "conversion_tests.hpp"

struct Person {
    [[=srl::tag::field{.id = 1}]]
    int age{};

    [[=srl::tag::field{.id = 2}]]
    double score{};

    int runtime_cookie{41};
};

struct [[=srl::tag::atomic{}]] Vec2 {
    float x{};
    float y{};
};

using Vec2Alias = Vec2;

struct [[=srl::tag::atomic{}]] Rect {
    Vec2 minimum{};
    Vec2 maximum{};
};

struct [[=srl::tag::atomic{.propagate = false}]] NonPropagatingAtomic {
    std::uint32_t value{};
};

struct [[=srl::tag::atomic{}]] ContainsNonPropagatingAtomic {
    NonPropagatingAtomic value{};
};

struct [[=srl::tag::atomic{.allow_padding = true}]] PaddedAtomic {
    std::uint8_t small{};
    std::uint32_t large{};
};

struct [[=srl::tag::atomic{}]] UniqueAtomic {
    std::uint32_t first{};
    std::uint32_t second{};
};

struct [[=srl::tag::atomic{}]] NonTrivialMemberwiseAtomic {
    float value{};

    ~NonTrivialMemberwiseAtomic() {}
};

struct [[=srl::tag::atomic{.allow_padding = true}]] NonTrivialPaddedAtomic {
    std::string value{};
};

struct [[=srl::tag::atomic{}]] SmallText {
    std::string value{};
};

namespace srl {
    template<>
    struct atomic_codec<SmallText> {
        static constexpr std::size_t fixed_size = std::dynamic_extent;

        [[nodiscard]] static std::expected<std::size_t, error> encoded_size(
            const SmallText &value) noexcept {
            if (value.value.size() > std::numeric_limits<std::uint32_t>::max() - sizeof(std::uint32_t)) {
                return detail::fail(error_code::entry_too_large);
            }
            return sizeof(std::uint32_t) + value.value.size();
        }

        [[nodiscard]] static std::expected<void, error> encode(
            const SmallText &value,
            std::span<std::byte> destination) noexcept {
            auto size = encoded_size(value);
            if (!size) {
                return std::unexpected{size.error()};
            }
            if (destination.size() != *size) {
                return detail::fail(error_code::invalid_payload_shape);
            }
            detail::write_trivial(destination, static_cast<std::uint32_t>(value.value.size()));
            const auto source = std::as_bytes(std::span{value.value.data(), value.value.size()});
            std::ranges::copy(source, destination.begin() + sizeof(std::uint32_t));
            return {};
        }

        [[nodiscard]] static std::expected<void, error> decode(
            std::span<const std::byte> source,
            SmallText &out) {
            if (source.size() < sizeof(std::uint32_t)) {
                return detail::fail(error_code::invalid_payload_shape);
            }
            const auto size = detail::read_trivial<std::uint32_t>(source);
            if (size != source.size() - sizeof(std::uint32_t)) {
                return detail::fail(error_code::invalid_payload_shape);
            }
            const auto bytes = source.subspan(sizeof(std::uint32_t));
            out.value.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            return {};
        }
    };
}

enum class activity : std::uint8_t {
    idle = 1,
    running = 4,
    sleeping = 9,
};

struct Profile {
    [[=srl::tag::field{.id = 1}]]
    Person person{};

    [[=srl::tag::field{.id = 2}]]
    std::string name{};

    [[=srl::tag::field{.id = 3}]]
    bool active{};

    [[=srl::tag::field{.id = 4}]]
    activity state{activity::idle};


    [[=srl::tag::field{.id = 5}]]
    Vec2 position{};

    void *runtime_handle{};
};

struct Pair {
    [[=srl::tag::field{.id = 1}]]
    int first{};

    [[=srl::tag::field{.id = 2}]]
    int second{};
};

struct FirstOnly {
    [[=srl::tag::field{.id = 1}]]
    int first{};
};

struct DuplicateIds {
    [[=srl::tag::field{.id = 1}]]
    int first{};

    [[=srl::tag::field{.id = 1}]]
    int second{};
};

struct MarkedPointer {
    [[=srl::tag::field{.id = 1}]]
    int *pointer{};
};

struct [[=srl::tag::atomic{}]] AtomicPointer {
    int *pointer{};
};

struct [[=srl::tag::atomic{.bypass_pointer_check = true}]] BypassedAtomicPointer {
    int *pointer{};
};

struct [[=srl::tag::atomic{}]] UnsupportedVariableAtomic {
    std::string value{};
};

struct [[=srl::tag::atomic{}]] AmbiguousAtomic {
    [[=srl::tag::field{.id = 1}]]
    int value{};
};

struct VectorField {
    [[=srl::tag::field{.id = 1}]]
    std::vector<int> values{};
};

struct RejectingSetField {
    [[=srl::tag::field{.id = 1}]]
    [[=srl::tag::container{.duplicates = srl::duplicate_policy::reject}]]
    std::set<int> values{};
};

struct ThirdPartyTrivial {
    std::uint32_t first{};
    std::uint32_t second{};

    constexpr auto operator<=>(const ThirdPartyTrivial &) const noexcept = default;
};

struct ForcedAtomicField {
    [[=srl::tag::field{.id = 1}]]
    [[=srl::tag::atomic{}]]
    ThirdPartyTrivial value{};
};

struct ForcedPointerField {
    [[=srl::tag::field{.id = 1}]]
    [[=srl::tag::atomic{}]]
    int *value{};
};

struct BypassedPointerField {
    [[=srl::tag::field{.id = 1}]]
    [[=srl::tag::atomic{.bypass_pointer_check = true}]]
    int *value{};
};

struct RawArrayField {
    [[=srl::tag::field{.id = 1}]]
    int values[3]{};
};

struct TrackingSequence {
    std::vector<int> values{};
    std::size_t reserved{};

    [[nodiscard]] auto begin() noexcept { return values.begin(); }
    [[nodiscard]] auto end() noexcept { return values.end(); }
    [[nodiscard]] auto begin() const noexcept { return values.begin(); }
    [[nodiscard]] auto end() const noexcept { return values.end(); }
};

namespace srl {
    template<>
    struct container_traits<TrackingSequence> : default_container_traits<TrackingSequence> {
        static void clear(TrackingSequence &value) {
            value.values.clear();
        }

        static void reserve(TrackingSequence &value, std::size_t size) {
            value.reserved = size;
            value.values.reserve(size);
        }

        static void insert(TrackingSequence &value, int element) {
            value.values.push_back(element);
        }
    };
}

static_assert(srl::schema_valid<Person>());
static_assert(srl::schema_valid<Vec2>());
static_assert(srl::schema_valid<Rect>());
static_assert(srl::schema_valid<NonPropagatingAtomic>());
static_assert(!srl::schema_valid<ContainsNonPropagatingAtomic>());
static_assert(srl::schema_valid<PaddedAtomic>());
static_assert(srl::schema_valid<UniqueAtomic>());
static_assert(srl::schema_valid<NonTrivialMemberwiseAtomic>());
static_assert(!srl::schema_valid<NonTrivialPaddedAtomic>());
static_assert(srl::schema_valid<SmallText>());
static_assert(srl::schema_valid<Profile>());
static_assert(srl::schema_valid<Pair>());
static_assert(srl::schema_valid<FirstOnly>());
static_assert(!srl::schema_valid<DuplicateIds>());
static_assert(!srl::schema_valid<MarkedPointer>());
static_assert(!srl::schema_valid<AtomicPointer>());
static_assert(srl::schema_valid<BypassedAtomicPointer>());
static_assert(!srl::schema_valid<UnsupportedVariableAtomic>());
static_assert(!srl::schema_valid<AmbiguousAtomic>());
static_assert(srl::schema_valid<int[3]>());
static_assert(!srl::schema_valid<int *[3]>());
static_assert(srl::schema_valid<std::array<int, 3>>());
static_assert(srl::schema_valid<std::vector<int>>());
static_assert(srl::schema_valid<std::deque<std::string>>());
static_assert(srl::schema_valid<std::list<Person>>());
static_assert(srl::schema_valid<std::forward_list<int>>());
static_assert(srl::schema_valid<std::set<int>>());
static_assert(srl::schema_valid<std::multiset<int>>());
static_assert(srl::schema_valid<std::map<int, std::string>>());
static_assert(srl::schema_valid<std::multimap<int, std::string>>());
static_assert(srl::schema_valid<std::unordered_set<int>>());
static_assert(srl::schema_valid<std::unordered_multiset<int>>());
static_assert(srl::schema_valid<std::unordered_map<int, std::string>>());
static_assert(srl::schema_valid<std::unordered_multimap<int, std::string>>());
static_assert(srl::schema_valid<VectorField>());
static_assert(srl::schema_valid<RejectingSetField>());
static_assert(srl::schema_valid<TrackingSequence>());
static_assert(!srl::schema_valid<ThirdPartyTrivial>());
static_assert(!srl::atomic_serializable<ThirdPartyTrivial>);
static_assert(srl::schema_valid<ForcedAtomicField>());
static_assert(!srl::schema_valid<ForcedPointerField>());
static_assert(srl::schema_valid<BypassedPointerField>());
static_assert(srl::schema_valid<RawArrayField>());
static_assert(srl::atomic_codec<Vec2>::fixed_size == sizeof(float) * 2);
static_assert(srl::atomic_codec<Rect>::fixed_size == sizeof(float) * 4);
static_assert(srl::atomic_propagates<int>());
static_assert(srl::atomic_propagates<Vec2>());
static_assert(!srl::atomic_propagates<NonPropagatingAtomic>());
static_assert(!srl::atomic_codec<Vec2>::uses_direct_object_representation);
static_assert(!std::has_unique_object_representations_v<PaddedAtomic>);
static_assert(srl::atomic_codec<PaddedAtomic>::uses_direct_object_representation);
static_assert(srl::atomic_codec<PaddedAtomic>::fixed_size == sizeof(PaddedAtomic));
static_assert(std::has_unique_object_representations_v<UniqueAtomic>);
static_assert(srl::atomic_codec<UniqueAtomic>::uses_direct_object_representation);
static_assert(srl::atomic_codec<UniqueAtomic>::fixed_size == sizeof(UniqueAtomic));
static_assert(!std::is_trivially_copyable_v<NonTrivialMemberwiseAtomic>);
static_assert(!srl::atomic_codec<NonTrivialMemberwiseAtomic>::uses_direct_object_representation);
static_assert(srl::atomic_codec<NonTrivialMemberwiseAtomic>::fixed_size == sizeof(float));
static_assert(srl::type_hash<Vec2Alias>() == srl::type_hash<Vec2>());
static_assert(srl::type_hash(^^Vec2Alias) == srl::type_hash(^^Vec2));
static_assert(srl::type_hash<Vec2>() == srl::type_hash(^^Vec2));
static_assert(srl::type_hash<int>() != srl::type_hash<unsigned int>());
static_assert(srl::enum_name(activity::running) == "running");
static_assert(srl::message(srl::error_code::missing_field) == "missing_field");

namespace {
    void require(bool condition, std::string_view description) {
        if (!condition) {
            throw std::runtime_error(std::string{description});
        }
    }

    template<typename T>
    void require_error(const std::expected<T, srl::error> &result,
                       srl::error_code expected,
                       std::string_view description) {
        require(!result && result.error().code == expected, description);
    }

    void set_u32(srl::byte_buffer &buffer, std::size_t offset, std::uint32_t value) {
        srl::detail::write_trivial(buffer.span().subspan(offset), value);
    }

    [[nodiscard]] std::uint32_t get_u32(const srl::byte_buffer &buffer, std::size_t offset) {
        return srl::detail::read_trivial<std::uint32_t>(buffer.span().subspan(offset));
    }

    void set_file_byte(const std::filesystem::path &path, std::size_t offset, std::byte value) {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        require(file.is_open(), "failed to open test block for mutation");
        file.seekp(static_cast<std::streamoff>(offset));
        const auto character = static_cast<char>(std::to_integer<unsigned char>(value));
        file.write(&character, 1);
        require(static_cast<bool>(file), "failed to mutate test block");
    }

    void test_record_round_trips() {
        const Person source{
            .age = 37,
            .score = 98.5,
            .runtime_cookie = 9001,
        };
        const auto packed = srl::pack(source);
        require(packed.has_value(), "Person packing failed");
        const auto packed_again = srl::pack(source);
        require(packed_again.has_value() && *packed == *packed_again,
                "packing must be deterministic");

        const auto extracted = srl::extract<Person>(packed->span());
        require(extracted.has_value(), "Person extraction failed");
        require(extracted->age == source.age && extracted->score == source.score,
                "serialized Person fields changed");
        require(extracted->runtime_cookie == 41,
                "unmarked fields must retain defaults during extract");

        Person target{.runtime_cookie = 777};
        const auto unpacked = srl::unpack(packed->span(), target);
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
        const auto profile_bytes = srl::pack(profile);
        require(profile_bytes.has_value(), "Profile packing failed");
        const auto profile_copy = srl::extract<Profile>(profile_bytes->span());
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
        const auto vector_bytes = srl::pack(vector);
        require(vector_bytes.has_value(), "Vec2 packing failed");
        require(vector_bytes->size() == srl::detail::entry_head_wire_size + sizeof(float) * 2,
                "Vec2 must be stored as direct atomic content");
        require(get_u32(*vector_bytes, 4) == 0, "atomic entry must not contain mappings");
        require(std::to_integer<std::uint8_t>((*vector_bytes)[9])
                    == std::to_underlying(srl::entry_kind::atomic)
                && std::to_integer<std::uint8_t>((*vector_bytes)[10])
                       == std::to_underlying(srl::mapping_kind::none)
                && std::to_integer<std::uint8_t>((*vector_bytes)[11]) == 0,
                "atomic entry classification is incorrect");
        const auto vector_copy = srl::extract<Vec2>(vector_bytes->span());
        require(vector_copy.has_value()
                && vector_copy->x == vector.x
                && vector_copy->y == vector.y,
                "Vec2 atomic round trip failed");

        const Rect rectangle{
            .minimum = Vec2{.x = -1.0f, .y = -2.0f},
            .maximum = Vec2{.x = 4.0f, .y = 8.0f},
        };
        const auto rectangle_bytes = srl::pack(rectangle);
        require(rectangle_bytes.has_value(), "Rect packing failed");
        const auto rectangle_copy = srl::extract<Rect>(rectangle_bytes->span());
        require(rectangle_copy.has_value()
                && rectangle_copy->minimum.x == rectangle.minimum.x
                && rectangle_copy->maximum.y == rectangle.maximum.y,
                "nested atomic members failed to round trip");

        const SmallText text{.value = "custom atomic codec"};
        const auto text_bytes = srl::pack(text);
        require(text_bytes.has_value(), "custom atomic packing failed");
        const auto text_copy = srl::extract<SmallText>(text_bytes->span());
        require(text_copy.has_value() && text_copy->value == text.value,
                "custom atomic codec failed to round trip");

        const PaddedAtomic padded{.small = 7, .large = 0x11223344u};
        const auto padded_bytes = srl::pack(padded);
        require(padded_bytes.has_value()
                && padded_bytes->size() == srl::detail::entry_head_wire_size + sizeof(PaddedAtomic),
                "padding-enabled atomic size is incorrect");
        require(std::memcmp(
                    padded_bytes->data() + srl::detail::entry_head_wire_size,
                    &padded,
                    sizeof(PaddedAtomic)) == 0,
                "padding-enabled atomic did not copy the complete object representation");
        const auto padded_copy = srl::extract<PaddedAtomic>(padded_bytes->span());
        require(padded_copy.has_value()
                && padded_copy->small == padded.small
                && padded_copy->large == padded.large,
                "padding-enabled atomic failed to round trip");

        const UniqueAtomic unique{.first = 11, .second = 22};
        const auto unique_bytes = srl::pack(unique);
        require(unique_bytes.has_value(), "unique-representation atomic packing failed");
        const auto unique_copy = srl::extract<UniqueAtomic>(unique_bytes->span());
        require(unique_copy.has_value()
                && unique_copy->first == unique.first
                && unique_copy->second == unique.second,
                "unique-representation atomic failed to round trip");

        const NonTrivialMemberwiseAtomic non_trivial{.value = 6.25f};
        const auto non_trivial_bytes = srl::pack(non_trivial);
        require(non_trivial_bytes.has_value()
                && non_trivial_bytes->size()
                       == srl::detail::entry_head_wire_size + sizeof(float),
                "non-trivial memberwise atomic size is incorrect");
        const auto non_trivial_copy =
            srl::extract<NonTrivialMemberwiseAtomic>(non_trivial_bytes->span());
        require(non_trivial_copy.has_value()
                && non_trivial_copy->value == non_trivial.value,
                "non-trivial memberwise atomic failed to round trip");

        int pointee = 42;
        const BypassedAtomicPointer bypassed{.pointer = &pointee};
        const auto bypassed_bytes = srl::pack(bypassed);
        require(bypassed_bytes.has_value(), "bypassed pointer atomic packing failed");
        const auto bypassed_copy = srl::extract<BypassedAtomicPointer>(
            bypassed_bytes->span());
        require(bypassed_copy.has_value() && bypassed_copy->pointer == &pointee,
                "bypassed pointer atomic failed to preserve its representation");

        const ForcedAtomicField forced{
            .value = ThirdPartyTrivial{.first = 0x11223344u, .second = 0x55667788u},
        };
        const auto forced_bytes = srl::pack(forced);
        require(forced_bytes.has_value(), "field-level forced atomic packing failed");
        const auto forced_copy = srl::extract<ForcedAtomicField>(forced_bytes->span());
        require(forced_copy.has_value() && forced_copy->value == forced.value,
                "field-level forced atomic failed to round trip");
    }

    void test_mapping_layout() {
        const auto bytes = srl::pack(Pair{
            .first = 0x11223344,
            .second = 0x55667788,
        });
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

        const auto direct_size = srl::serialized_size(Pair{.first = 0x11223344, .second = 0x55667788});
        require(direct_size.has_value() && *direct_size == bytes->size(),
                "serialized_size did not match pack output");
        srl::byte_buffer direct;
        direct.resize(*direct_size);
        require(srl::pack_into(
                    direct.span(),
                    Pair{.first = 0x11223344, .second = 0x55667788}).has_value()
                && direct == *bytes,
                "pack_into did not write directly into the supplied span");
    }

    void test_compatible_views() {
        const auto full = srl::pack(Pair{.first = 10, .second = 20});
        require(full.has_value(), "full view packing failed");
        const auto subset = srl::extract<FirstOnly>(full->span());
        require(subset.has_value() && subset->first == 10,
                "unknown stored fields must be ignored");

        const auto partial = srl::pack(FirstOnly{.first = 10});
        require(partial.has_value(), "partial view packing failed");
        require_error(srl::extract<Pair>(partial->span()),
                      srl::error_code::missing_field,
                      "missing required field was accepted");
    }

    void test_container_round_trips() {
        const std::vector<int> vector{1, 2, 3, 5, 8};
        const auto vector_bytes = srl::pack(vector);
        require(vector_bytes.has_value(), "vector packing failed");
        require(std::to_integer<std::uint8_t>((*vector_bytes)[9])
                    == std::to_underlying(srl::entry_kind::container)
                && std::to_integer<std::uint8_t>((*vector_bytes)[10])
                       == std::to_underlying(srl::mapping_kind::element)
                && std::to_integer<std::uint8_t>((*vector_bytes)[11]) == 1,
                "single-value container classification is incorrect");
        const auto vector_copy = srl::extract<std::vector<int>>(vector_bytes->span());
        require(vector_copy.has_value() && *vector_copy == vector,
                "vector round trip failed");

        const auto list_copy = srl::extract<std::list<int>>(vector_bytes->span());
        require(list_copy.has_value() && std::ranges::equal(*list_copy, vector),
                "compatible vector-to-list decode failed");

        const std::forward_list<int> forward{4, 3, 2, 1};
        const auto forward_bytes = srl::pack(forward);
        require(forward_bytes.has_value(), "forward_list packing failed");
        const auto forward_copy = srl::extract<std::forward_list<int>>(forward_bytes->span());
        require(forward_copy.has_value() && *forward_copy == forward,
                "forward_list order changed");

        const std::array<int, 3> array{9, 8, 7};
        const auto array_bytes = srl::pack(array);
        require(array_bytes.has_value(), "array packing failed");
        const auto array_copy = srl::extract<std::array<int, 3>>(array_bytes->span());
        require(array_copy.has_value() && *array_copy == array,
                "array round trip failed");
        require_error(srl::extract<std::array<int, 2>>(array_bytes->span()),
                      srl::error_code::fixed_container_size_mismatch,
                      "array element-count mismatch was accepted");

        int native_array[3]{6, 4, 2};
        const auto native_array_bytes = srl::pack(native_array);
        require(native_array_bytes.has_value(), "native array packing failed");
        int native_array_copy[3]{};
        require(srl::unpack(native_array_bytes->span(), native_array_copy).has_value()
                && std::ranges::equal(native_array, native_array_copy),
                "native array round trip failed");

        const RawArrayField raw_field{.values = {3, 1, 4}};
        const auto raw_field_bytes = srl::pack(raw_field);
        require(raw_field_bytes.has_value(), "native array field packing failed");
        const auto raw_field_copy = srl::extract<RawArrayField>(raw_field_bytes->span());
        require(raw_field_copy.has_value()
                && std::ranges::equal(raw_field.values, raw_field_copy->values),
                "native array field round trip failed");

        const std::vector<std::vector<int>> nested{{1, 2}, {}, {3, 4, 5}};
        const auto nested_bytes = srl::pack(nested);
        require(nested_bytes.has_value(), "nested container packing failed");
        const auto nested_copy =
            srl::extract<std::vector<std::vector<int>>>(nested_bytes->span());
        require(nested_copy.has_value() && *nested_copy == nested,
                "nested container round trip failed");

        const std::vector<bool> bits{true, false, true, true};
        const auto bit_bytes = srl::pack(bits);
        require(bit_bytes.has_value(), "vector<bool> packing failed");
        const auto bit_copy = srl::extract<std::vector<bool>>(bit_bytes->span());
        require(bit_copy.has_value() && *bit_copy == bits,
                "vector<bool> proxy elements failed to round trip");

        TrackingSequence tracking{.values = {6, 7, 8}};
        const auto tracking_bytes = srl::pack(tracking);
        require(tracking_bytes.has_value(), "custom container packing failed");
        const auto tracking_copy = srl::extract<TrackingSequence>(tracking_bytes->span());
        require(tracking_copy.has_value()
                && tracking_copy->values == tracking.values
                && tracking_copy->reserved == tracking.values.size(),
                "custom traits or reserve optimization failed");

        const std::map<int, std::string> map{{1, "one"}, {2, "two"}};
        const auto map_bytes = srl::pack(map);
        require(map_bytes.has_value(), "map packing failed");
        require(get_u32(*map_bytes, 4) == map.size() * 2
                && std::to_integer<std::uint8_t>((*map_bytes)[11]) == 2,
                "map key/value slots were not grouped in pairs");
        const auto unordered_copy = srl::extract<std::unordered_map<int, std::string>>(
            map_bytes->span());
        require(unordered_copy.has_value()
                && unordered_copy->at(1) == "one"
                && unordered_copy->at(2) == "two",
                "map-to-unordered_map decode failed");

        const std::vector<int> duplicates{3, 3, 5};
        const auto duplicate_bytes = srl::pack(duplicates);
        require(duplicate_bytes.has_value(), "duplicate sequence packing failed");
        const auto deduplicated = srl::extract<std::set<int>>(duplicate_bytes->span());
        require(deduplicated.has_value() && *deduplicated == std::set<int>{3, 5},
                "default unique-container deduplication failed");
        require_error(
            srl::extract<std::set<int>>(
                duplicate_bytes->span(),
                srl::decode_options{.duplicates = srl::duplicate_policy::reject}),
            srl::error_code::duplicate_container_element,
            "root duplicate rejection was not enforced");

        const auto field_bytes = srl::pack(VectorField{.values = duplicates});
        require(field_bytes.has_value(), "container field packing failed");
        require_error(srl::extract<RejectingSetField>(field_bytes->span()),
                      srl::error_code::duplicate_container_element,
                      "field container duplicate tag was not enforced");
    }

    void test_malformed_input() {
        const auto pair_bytes = srl::pack(Pair{.first = 10, .second = 20});
        require(pair_bytes.has_value(), "Pair packing failed");

        auto oversized = *pair_bytes;
        set_u32(oversized, 0, static_cast<std::uint32_t>(oversized.size() + 1));
        require_error(srl::extract<Pair>(oversized.span()),
                      srl::error_code::invalid_entry_size,
                      "oversized entry was accepted");

        auto trailing = *pair_bytes;
        trailing.push_back(std::byte{});
        require_error(srl::extract<Pair>(trailing.span()),
                      srl::error_code::trailing_bytes,
                      "trailing entry bytes were accepted");

        auto bad_alignment = *pair_bytes;
        bad_alignment[8] = std::byte{2};
        require_error(srl::extract<Pair>(bad_alignment.span()),
                      srl::error_code::unsupported_mapping_alignment,
                      "unsupported mapping alignment was accepted");

        auto bad_classification = *pair_bytes;
        bad_classification[9] = std::byte{};
        require_error(srl::extract<Pair>(bad_classification.span()),
                      srl::error_code::invalid_entry_classification,
                      "invalid entry classification was accepted");

        auto duplicate = *pair_bytes;
        set_u32(duplicate, 24, 1);
        require_error(srl::extract<Pair>(duplicate.span()),
                      srl::error_code::invalid_mapping_order,
                      "duplicate field id was accepted");

        auto bad_address = *pair_bytes;
        set_u32(bad_address, 20, std::numeric_limits<std::uint32_t>::max());
        require_error(srl::extract<Pair>(bad_address.span()),
                      srl::error_code::payload_out_of_bounds,
                      "out-of-bounds mapping was accepted");

        require_error(srl::extract<Pair>(pair_bytes->span().first(pair_bytes->size() - 1)),
                      srl::error_code::invalid_entry_size,
                      "truncated entry was accepted");

        const auto bool_bytes = srl::pack(true);
        require(bool_bytes.has_value(), "bool packing failed");
        auto invalid_bool = *bool_bytes;
        invalid_bool[srl::detail::entry_head_wire_size] = std::byte{2};
        require_error(srl::extract<bool>(invalid_bool.span()),
                      srl::error_code::invalid_bool,
                      "invalid bool payload was accepted");

        const auto enum_bytes = srl::pack(activity::sleeping);
        require(enum_bytes.has_value(), "enum packing failed");
        auto invalid_enum = *enum_bytes;
        invalid_enum[srl::detail::entry_head_wire_size] = std::byte{7};
        require_error(srl::extract<activity>(invalid_enum.span()),
                      srl::error_code::invalid_enum_value,
                      "invalid enum payload was accepted");

        const auto int_bytes = srl::pack(42);
        require(int_bytes.has_value(), "integer packing failed");
        require_error(srl::extract<unsigned int>(int_bytes->span()),
                      srl::error_code::type_mismatch,
                      "atomic root type mismatch was accepted");

        const auto map_bytes = srl::pack(std::map<int, int>{{1, 2}, {3, 4}});
        require(map_bytes.has_value(), "map packing failed");
        auto incomplete_pair = *map_bytes;
        set_u32(incomplete_pair, 4, 3);
        require_error(srl::extract<std::map<int, int>>(incomplete_pair.span()),
                      srl::error_code::invalid_container_shape,
                      "incomplete key/value mapping group was accepted");

        auto wrong_container_id = *map_bytes;
        set_u32(wrong_container_id, 16, 4);
        require_error(srl::extract<std::map<int, int>>(wrong_container_id.span()),
                      srl::error_code::invalid_container_shape,
                      "non-contiguous container mapping ids were accepted");
    }

    void test_io() {
        const auto path = std::filesystem::temp_directory_path() / "srl_stage1_test.bin";
        std::error_code ignored;
        std::filesystem::remove(path, ignored);

        const Profile profile{
            .person = Person{.age = 28, .score = 77.5, .runtime_cookie = 999},
            .name = "disk block",
            .active = true,
            .state = activity::sleeping,
            .position = Vec2{.x = 3.0f, .y = 6.0f},
        };
        const auto saved = srl::save(path, profile);
        require(saved.has_value(), "save failed");

        const auto block = srl::read_block(path);
        require(block.has_value(), "read_block failed");
        const auto packed = srl::pack(profile);
        require(packed.has_value() && *block == *packed,
                "disk block did not preserve serialized entry bytes");

        const auto loaded = srl::load<Profile>(path);
        require(loaded.has_value()
                && loaded->person.age == profile.person.age
                && loaded->name == profile.name
                && loaded->position.y == profile.position.y,
                "load failed");

        set_file_byte(path, 0, std::byte{});
        require_error(srl::read_block(path),
                      srl::error_code::invalid_storage_magic,
                      "invalid storage magic was accepted");

        require(srl::save(path, profile).has_value(), "failed to restore test block");
        set_file_byte(path, 8, std::byte{1});
        require_error(srl::read_block(path),
                      srl::error_code::unsupported_storage_version,
                      "unsupported storage version was accepted");

        require(srl::save(path, profile).has_value(), "failed to restore test block");
        set_file_byte(path, 12, std::byte{1});
        require_error(srl::read_block(path),
                      srl::error_code::unsupported_compression,
                      "unsupported compression was accepted");

        std::filesystem::remove(path, ignored);
        require_error(srl::read_block(path),
                      srl::error_code::io_open_failed,
                      "missing file was accepted");
    }
}

int main() {
    test_record_round_trips();
    test_atomic_round_trips();
    test_mapping_layout();
    test_compatible_views();
    test_container_round_trips();
    test_malformed_input();
    test_io();
    conversion_tests::run();
    std::println("serialization reflection tests passed");
}
