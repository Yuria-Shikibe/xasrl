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
    [[=xasrl::tag::field{.id = 1}]]
    int age{};

    [[=xasrl::tag::field{.id = 2}]]
    double score{};

    int runtime_cookie{41};
};

struct [[=xasrl::tag::auto_seq{}]] Vec2 {
    float x{};
    float y{};
};

using Vec2Alias = Vec2;

struct [[=xasrl::tag::auto_seq{}]] Rect {
    Vec2 minimum{};
    Vec2 maximum{};
};

struct [[=xasrl::tag::leaf{.allow_non_unique_presentation = true}]] PaddedLeaf {
    std::uint8_t small{};
    std::uint32_t large{};
};

struct [[=xasrl::tag::leaf{}]] UniqueLeaf {
    std::uint32_t first{};
    std::uint32_t second{};
};

struct [[=xasrl::tag::leaf{}]] NonTrivialMemberwiseLeaf {
    float value{};

    ~NonTrivialMemberwiseLeaf() {}
};

struct [[=xasrl::tag::leaf{}]] TransientLeaf {
    std::uint32_t first{};
    std::uint32_t second{};

    [[=xasrl::tag::transient{}]]
    std::uint32_t runtime_only{17};

    std::uint32_t third{};
    std::uint32_t fourth{};
};

struct [[=xasrl::tag::leaf{.allow_non_unique_presentation = true}]] TransientPointerLeaf {
    [[=xasrl::tag::transient{}]]
    int *runtime_pointer{};

    std::uint32_t value{};
};

struct InvalidRecordTransient {
    [[=xasrl::tag::field{.id = 1}]]
    std::uint32_t serialized{};

    [[=xasrl::tag::transient{}]]
    std::uint32_t runtime_only{};
};

struct [[=xasrl::tag::leaf{}]] LeafBase {
    float base_value{};
};

struct [[=xasrl::tag::leaf{}]] DerivedLeaf : LeafBase {
    std::uint32_t derived_value{};

    ~DerivedLeaf() {}
};

struct [[=xasrl::tag::leaf{}]] PrivateDerivedLeaf : private LeafBase {
    std::uint32_t derived_value{};

    void set_base_value(float value) {
        LeafBase::base_value = value;
    }

    [[nodiscard]] float get_base_value() const {
        return LeafBase::base_value;
    }

    ~PrivateDerivedLeaf() {}
};

struct [[=xasrl::tag::leaf{}]] ProtectedDerivedLeaf : protected LeafBase {
    std::uint32_t derived_value{};

    void set_base_value(float value) {
        LeafBase::base_value = value;
    }

    [[nodiscard]] float get_base_value() const {
        return LeafBase::base_value;
    }

    ~ProtectedDerivedLeaf() {}
};

struct [[=xasrl::tag::leaf{}]] VirtualDerivedLeaf : virtual LeafBase {
    std::uint32_t derived_value{};
};

struct [[=xasrl::tag::leaf{.allow_non_unique_presentation = true}]] NonTrivialPaddedLeaf {
    std::string value{};
};

struct [[=xasrl::tag::auto_seq{}]] SmallText {
    std::string value{};
};

struct [[=xasrl::tag::auto_seq{}]] AutoSequenceBase {
    std::uint32_t base_value{};
};

struct [[=xasrl::tag::auto_seq{}]] AutoSequenceRecord : AutoSequenceBase {
    std::uint32_t first{};

    [[=xasrl::tag::transient{}]]
    std::uint32_t runtime_only{17};

    [[=xasrl::tag::field{.id = 4}]]
    std::string name{};

    bool active{};
};

struct [[=xasrl::tag::auto_seq{}]] AutoSequenceDuplicate {
    std::uint32_t first{};

    [[=xasrl::tag::field{.id = 0}]]
    std::uint32_t second{};
};

struct [[=xasrl::tag::auto_seq{}]] AutoSequenceOverflow {
    [[=xasrl::tag::field{.id = std::numeric_limits<xasrl::id_type>::max()}]]
    std::uint32_t first{};

    std::uint32_t second{};
};

struct AutoSequencePointerBase {
    int *base_pointer{};
};

struct [[=xasrl::tag::auto_seq{}]] AutoSequenceEntityBypass
    : [[=xasrl::tag::bypass_pointer_check{}]] AutoSequencePointerBase {
    [[=xasrl::tag::bypass_pointer_check{}]]
    int *member_pointer{};

    std::uint32_t value{};
};

struct [[=xasrl::tag::auto_seq{}]]
       [[=xasrl::tag::bypass_pointer_check{}]] AutoSequenceTypeBypass {
    int *pointer{};
    std::uint32_t value{};
};

struct [[=xasrl::tag::auto_seq{}]] AutoSequenceRejectedPointer {
    int *pointer{};
};

struct [[=xasrl::tag::leaf{}]] LeafEntityBypass {
    [[=xasrl::tag::bypass_pointer_check{}]]
    int *pointer{};
};

struct LeafPointerBase {
    int *pointer{};
};

struct [[=xasrl::tag::leaf{}]] LeafBaseBypass
    : [[=xasrl::tag::bypass_pointer_check{}]] LeafPointerBase {};

struct [[=xasrl::tag::leaf{}]] RuntimeFixedSize {};
struct [[=xasrl::tag::leaf{}]] DynamicSpanCodec {};

namespace xasrl {
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
    [[=xasrl::tag::field{.id = 1}]]
    Person person{};

    [[=xasrl::tag::field{.id = 2}]]
    std::string name{};

    [[=xasrl::tag::field{.id = 3}]]
    bool active{};

    [[=xasrl::tag::field{.id = 4}]]
    activity state{activity::idle};


    [[=xasrl::tag::field{.id = 5}]]
    Vec2 position{};

    void *runtime_handle{};
};

struct Pair {
    [[=xasrl::tag::field{.id = 1}]]
    int first{};

    [[=xasrl::tag::field{.id = 2}]]
    int second{};
};

struct FirstOnly {
    [[=xasrl::tag::field{.id = 1}]]
    int first{};
};

struct DuplicateIds {
    [[=xasrl::tag::field{.id = 1}]]
    int first{};

    [[=xasrl::tag::field{.id = 1}]]
    int second{};
};

struct MarkedPointer {
    [[=xasrl::tag::field{.id = 1}]]
    int *pointer{};
};

struct [[=xasrl::tag::leaf{}]] LeafPointer {
    int *pointer{};
};

struct [[=xasrl::tag::leaf{}]]
       [[=xasrl::tag::bypass_pointer_check{}]] BypassedLeafPointer {
    int *pointer{};
};

struct [[=xasrl::tag::leaf{}]] UnsupportedVariableLeaf {
    std::string value{};
};

struct [[=xasrl::tag::leaf{}]] AmbiguousLeaf {
    [[=xasrl::tag::field{.id = 1}]]
    int value{};
};

struct VectorField {
    [[=xasrl::tag::field{.id = 1}]]
    std::vector<int> values{};
};

struct RejectingSetField {
    [[=xasrl::tag::field{.id = 1}]]
    [[=xasrl::tag::container{
        .duplicates = xasrl::duplicate_policy::reject}]]
    std::set<int> values{};
};

struct ThirdPartyTrivial {
    std::uint32_t first{};
    std::uint32_t second{};

    constexpr auto operator<=>(const ThirdPartyTrivial &) const noexcept = default;
};

struct ForcedLeafField {
    [[=xasrl::tag::field{.id = 1}]]
    [[=xasrl::tag::leaf{}]]
    ThirdPartyTrivial value{};
};

struct ForcedPointerField {
    [[=xasrl::tag::field{.id = 1}]]
    [[=xasrl::tag::leaf{}]]
    int *value{};
};

struct BypassedPointerField {
    [[=xasrl::tag::field{.id = 1}]]
    [[=xasrl::tag::leaf{}]]
    [[=xasrl::tag::bypass_pointer_check{}]]
    int *value{};
};

struct RawArrayField {
    [[=xasrl::tag::field{.id = 1}]]
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

namespace xasrl {
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

