#include <print>
#include <stdexcept>
#include <string>
#include <span>
#include <vector>

#include "srl/core.hpp"
#include "srl/schema_visualization.hpp"

template <typename T>
struct [[=srl::tag::leaf{}]] vec2 {
    T x, y;
};

struct PlayerProfile {
    [[=srl::tag::field{.id = 1}]]
    std::string name{};

    [[=srl::tag::field{.id = 2}]]
    std::vector<vec2<std::uint32_t>> vals{};

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

    std::println("schema \n {}", srl::schema_string(^^PlayerProfile));

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

    std::println("{} reached level {} ({} serialized bytes)",
                 copy.name,
                 copy.level,
                 bytes.size());
}
