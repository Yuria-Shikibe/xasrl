#pragma once

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
#include "srl/serialization.hpp"

struct Person {
    [[=srl::tag::field{.id = 1}]]
    int age{};

    [[=srl::tag::field{.id = 2}]]
    double score{};

    int runtime_cookie{41};
};

struct [[=srl::tag::auto_seq{}]] Vec2 {
    float x{};
    float y{};
};

using Vec2Alias = Vec2;

struct [[=srl::tag::auto_seq{}]] Rect {
    Vec2 minimum{};
    Vec2 maximum{};
};

struct [[=srl::tag::leaf{.allow_padding = true}]] PaddedLeaf {
    std::uint8_t small{};
    std::uint32_t large{};
};

struct [[=srl::tag::leaf{}]] UniqueLeaf {
    std::uint32_t first{};
    std::uint32_t second{};
};

struct [[=srl::tag::leaf{}]] NonTrivialMemberwiseLeaf {
    float value{};

    ~NonTrivialMemberwiseLeaf() {}
};

struct [[=srl::tag::leaf{}]] TransientLeaf {
    std::uint32_t first{};
    std::uint32_t second{};

    [[=srl::tag::transient{}]]
    std::uint32_t runtime_only{17};

    std::uint32_t third{};
    std::uint32_t fourth{};
};

struct [[=srl::tag::leaf{.allow_padding = true}]] TransientPointerLeaf {
    [[=srl::tag::transient{}]]
    int *runtime_pointer{};

    std::uint32_t value{};
};

struct InvalidRecordTransient {
    [[=srl::tag::field{.id = 1}]]
    std::uint32_t serialized{};

    [[=srl::tag::transient{}]]
    std::uint32_t runtime_only{};
};

struct [[=srl::tag::leaf{}]] LeafBase {
    float base_value{};
};

struct [[=srl::tag::leaf{}]] DerivedLeaf : LeafBase {
    std::uint32_t derived_value{};

    ~DerivedLeaf() {}
};

struct [[=srl::tag::leaf{}]] PrivateDerivedLeaf : private LeafBase {
    std::uint32_t derived_value{};

    void set_base_value(float value) {
        LeafBase::base_value = value;
    }

    [[nodiscard]] float get_base_value() const {
        return LeafBase::base_value;
    }

    ~PrivateDerivedLeaf() {}
};

struct [[=srl::tag::leaf{}]] ProtectedDerivedLeaf : protected LeafBase {
    std::uint32_t derived_value{};

    void set_base_value(float value) {
        LeafBase::base_value = value;
    }

    [[nodiscard]] float get_base_value() const {
        return LeafBase::base_value;
    }

    ~ProtectedDerivedLeaf() {}
};

struct [[=srl::tag::leaf{}]] VirtualDerivedLeaf : virtual LeafBase {
    std::uint32_t derived_value{};
};

struct [[=srl::tag::leaf{.allow_padding = true}]] NonTrivialPaddedLeaf {
    std::string value{};
};

struct [[=srl::tag::auto_seq{}]] SmallText {
    std::string value{};
};

struct [[=srl::tag::auto_seq{}]] AutoSequenceBase {
    std::uint32_t base_value{};
};

struct [[=srl::tag::auto_seq{}]] AutoSequenceRecord : AutoSequenceBase {
    std::uint32_t first{};

    [[=srl::tag::transient{}]]
    std::uint32_t runtime_only{17};

    [[=srl::tag::field{.id = 4}]]
    std::string name{};

    bool active{};
};

struct [[=srl::tag::auto_seq{}]] AutoSequenceDuplicate {
    std::uint32_t first{};

    [[=srl::tag::field{.id = 0}]]
    std::uint32_t second{};
};

struct [[=srl::tag::auto_seq{}]] AutoSequenceOverflow {
    [[=srl::tag::field{.id = std::numeric_limits<srl::id_type>::max()}]]
    std::uint32_t first{};

    std::uint32_t second{};
};

struct AutoSequencePointerBase {
    int *base_pointer{};
};

struct [[=srl::tag::auto_seq{}]] AutoSequenceEntityBypass
    : [[=srl::tag::bypass_pointer_check{}]] AutoSequencePointerBase {
    [[=srl::tag::bypass_pointer_check{}]]
    int *member_pointer{};

    std::uint32_t value{};
};

struct [[=srl::tag::auto_seq{}]]
       [[=srl::tag::bypass_pointer_check{}]] AutoSequenceTypeBypass {
    int *pointer{};
    std::uint32_t value{};
};

struct [[=srl::tag::auto_seq{}]] AutoSequenceRejectedPointer {
    int *pointer{};
};

struct [[=srl::tag::leaf{}]] LeafEntityBypass {
    [[=srl::tag::bypass_pointer_check{}]]
    int *pointer{};
};

struct LeafPointerBase {
    int *pointer{};
};

struct [[=srl::tag::leaf{}]] LeafBaseBypass
    : [[=srl::tag::bypass_pointer_check{}]] LeafPointerBase {};

struct [[=srl::tag::leaf{}]] RuntimeFixedSize {};
struct [[=srl::tag::leaf{}]] DynamicSpanCodec {};

namespace srl {
    template<>
    struct leaf_codec<RuntimeFixedSize> {
        static inline std::size_t fixed_size = 0;

        [[nodiscard]] static error encode(
            const RuntimeFixedSize &,
            std::span<std::byte>) noexcept {
            return {};
        }

        [[nodiscard]] static error decode(
            std::span<const std::byte>,
            RuntimeFixedSize &) noexcept {
            return {};
        }
    };

    template<>
    struct leaf_codec<DynamicSpanCodec> {
        static constexpr std::size_t fixed_size = 0;

        [[nodiscard]] static error encode(
            const DynamicSpanCodec &,
            std::span<std::byte>) noexcept {
            return {};
        }

        [[nodiscard]] static error decode(
            std::span<const std::byte>,
            DynamicSpanCodec &) noexcept {
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

struct [[=srl::tag::leaf{}]] LeafPointer {
    int *pointer{};
};

struct [[=srl::tag::leaf{}]]
       [[=srl::tag::bypass_pointer_check{}]] BypassedLeafPointer {
    int *pointer{};
};

struct [[=srl::tag::leaf{}]] UnsupportedVariableLeaf {
    std::string value{};
};

struct [[=srl::tag::leaf{}]] AmbiguousLeaf {
    [[=srl::tag::field{.id = 1}]]
    int value{};
};

struct VectorField {
    [[=srl::tag::field{.id = 1}]]
    std::vector<int> values{};
};

struct RejectingSetField {
    [[=srl::tag::field{.id = 1}]]
    [[=srl::tag::container{
        .duplicates = srl::duplicate_policy::reject}]]
    std::set<int> values{};
};

struct ThirdPartyTrivial {
    std::uint32_t first{};
    std::uint32_t second{};

    constexpr auto operator<=>(const ThirdPartyTrivial &) const noexcept = default;
};

struct ForcedLeafField {
    [[=srl::tag::field{.id = 1}]]
    [[=srl::tag::leaf{}]]
    ThirdPartyTrivial value{};
};

struct ForcedPointerField {
    [[=srl::tag::field{.id = 1}]]
    [[=srl::tag::leaf{}]]
    int *value{};
};

struct BypassedPointerField {
    [[=srl::tag::field{.id = 1}]]
    [[=srl::tag::leaf{}]]
    [[=srl::tag::bypass_pointer_check{}]]
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

template<std::size_t Size>
struct FixedSequence {
    int values[Size]{};

    [[nodiscard]] constexpr auto begin() noexcept { return std::ranges::begin(values); }
    [[nodiscard]] constexpr auto end() noexcept { return std::ranges::end(values); }
    [[nodiscard]] constexpr auto begin() const noexcept { return std::ranges::begin(values); }
    [[nodiscard]] constexpr auto end() const noexcept { return std::ranges::end(values); }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return Size; }

    bool operator==(const FixedSequence &) const = default;
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

