#include <print>
#include <stdexcept>
#include <string>
#include <span>
#include <vector>

#include "srl/core.hpp"
#include "srl/number_to_string.hpp"
#include "srl/schema_visualization.hpp"

template <typename T>
struct [[=srl::tag::leaf{.allow_non_unique_presentation = true}]] vec2 {
    T x, y;
};



struct [[=srl::tag::leaf{.allow_non_unique_presentation = true}]] trans2 {
    vec2<float> pos;
    float rot;
};


struct PlayerProfile {
    [[=srl::tag::field{.id = 1}]]
    std::string name{};

    [[=srl::tag::field{.id = 2}]]
    std::vector<vec2<std::uint32_t>> vals{};

    [[=srl::tag::field{.id = 5}]]
    std::vector<vec2<trans2>> valss{};

    [[=srl::tag::field{.id = 4}]]
    trans2 trans{};

    [[=srl::tag::field{.id = 3}]]
    int level{};
};

int main() {

    const srl::serialization_context context;
    const PlayerProfile source{
        .name = "reflection demo",
        .vals = {{1, 2}, {5, 4}, {2, 3}},
        .level = 26,
    };

    std::println("Schema (compact):\n{0}\n\nSchema (detailed):\n{0:d}"
                 "\n\nSchema (memory, default 8B/row):\n{0:b}"
                 "\n\nSchema (memory, 4B/row):\n{0:b2}",
                 srl::schema_view<^^PlayerProfile>{});

    srl::byte_buffer bytes{};
    const auto bytes_status = srl::serialize(source, bytes, context);
    if (bytes_status) {
        throw std::runtime_error(std::string{srl::message(bytes_status.code)});
    }

    PlayerProfile copy{};
    const auto copy_status = srl::deserialize(bytes.span(), copy, context);
    if (copy_status) {
        throw std::runtime_error(std::string{srl::message(copy_status.code)});
    }

    const auto level = srl::number_to_string(copy.level);
    const auto size = srl::number_to_string(bytes.size());
    std::println("{} reached level {} ({} serialized bytes)", copy.name, level.view(), size.view());
}
