#include <print>
#include <stdexcept>
#include <string>
#include <span>
#include <utility>
#include <vector>

#include "srl/core.hpp"
#include "srl/mem/struct_transform.hpp"
#include "srl/number_to_string.hpp"
#include "srl/schema_visualization.hpp"


struct [[=xasrl::tag::leaf{}]] vec2 {
    int x, y;
};

struct PlayerProfile {
    [[=xasrl::tag::field{.id = 1}]]
    std::string name{};

    [[=xasrl::tag::field{.id = 2}]]
    std::vector<vec2> vals{};

    [[=xasrl::tag::field{.id = 3}]]
    int level{};

    std::string label{};
};

struct PlayerProfileView {
    PlayerProfileView &operator=(const PlayerProfileView &) = default;

    [[=xasrl::tag::accept_member{^^PlayerProfile::label}]]
    std::string display_name{};

    [[=xasrl::tag::field{30}, =xasrl::tag::accept_id{3}]]
    long long rank{};

    [[=xasrl::tag::field{31}]]
    int lives{3};

};

int main() {

    const xasrl::serialization_context context;
    PlayerProfile source{
        .name = "reflection demo",
        .vals = {{1, 2}, {5, 4}, {2, 3}},
        .level = 26,
        .label = "arena profile",
    };

    PlayerProfileView view;
    constexpr auto v = xasrl::schema{^^PlayerProfileView}.value_node().kind();
    xasrl::transform_assign(view, std::as_const(source));

    PlayerProfileView moved;
    xasrl::transform_assign(moved, std::move(source));
    std::println("In-memory move: {}, rank {}, lives {}",
                 moved.display_name, moved.rank, moved.lives);
}
