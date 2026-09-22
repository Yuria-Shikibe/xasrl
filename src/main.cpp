#include <print>
#include <stdexcept>
#include <string>
#include <span>

#include "srl/core.hpp"

template <typename T>
struct [[=srl::tag::atomic{}]] vec2 {
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

    std::println("schema \n {}", srl::schema_string<PlayerProfile>());

    const auto bytes = srl::pack(source, context);
    if (!bytes) {
        throw std::runtime_error(std::string{srl::message(bytes.error().code)});
    }

    const auto copy = srl::extract<PlayerProfile>(bytes->span(), context);
    if (!copy) {
        throw std::runtime_error(std::string{srl::message(copy.error().code)});
    }

    std::println("{} reached level {} ({} serialized bytes)",
                 copy->name,
                 copy->level,
                 bytes->size());
}
