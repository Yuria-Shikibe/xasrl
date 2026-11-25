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

template <typename T>
struct [[=xasrl::tag::leaf{.allow_non_unique_presentation = true}]] vec2 {
    T x, y;
};



struct [[=xasrl::tag::leaf{.allow_non_unique_presentation = true}]] trans2 {
    vec2<float> pos;
    float rot;
};


struct [[=xasrl::tag::auto_seq{}]] PlayerVitals {
    int health{80};
};

struct PlayerProfile : PlayerVitals {
    [[=xasrl::tag::field{.id = 1}]]
    std::string name{};

    [[=xasrl::tag::field{.id = 2}]]
    std::vector<vec2<std::uint32_t>> vals{};

    [[=xasrl::tag::field{.id = 5}]]
    std::vector<vec2<trans2>> valss{};

    [[=xasrl::tag::field{.id = 4}]]
    trans2 trans{};

    [[=xasrl::tag::field{.id = 3}]]
    int level{};

    std::string label{};
};

struct PlayerProfileView {
    PlayerProfileView &operator=(const PlayerProfileView &) = default;

    [[=xasrl::tag::accept_member{^^PlayerProfile::label}]]
    std::string display_name{};

    [[=xasrl::tag::accept_id{3}]]
    long long rank{};

    [[=xasrl::tag::field{31}]]
    int lives{3};

    [[=xasrl::tag::accept_member{^^PlayerProfile, ^^PlayerVitals}]]
    PlayerVitals vitals{.health = 0};
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
    xasrl::transform_assign(view, std::as_const(source));
    std::println("In-memory copy: {}, rank {}, lives {}, health {}",
                 view.display_name, view.rank, view.lives, view.vitals.health);

    std::println("Schema (compact):\n{0}\n\nSchema (detailed):\n{0:d}"
                 "\n\nSchema (memory, default 8B/row):\n{0:b}"
                 "\n\nSchema (memory, 4B/row):\n{0:b2}",
                 xasrl::schema_view<^^PlayerProfile>{});

    xasrl::byte_buffer bytes{};
    const auto bytes_status = xasrl::serialize(source, bytes, xasrl::srl_context{context});
    if (bytes_status) {
        throw std::runtime_error(std::string{xasrl::message(bytes_status.code)});
    }

    PlayerProfile copy{};
    const auto copy_status = xasrl::deserialize(bytes.span(), copy, context);
    if (copy_status) {
        throw std::runtime_error(std::string{xasrl::message(copy_status.code)});
    }

    const auto level = xasrl::number_to_string(copy.level);
    const auto size = xasrl::number_to_string(bytes.size());
    std::println("{} reached level {} ({} serialized bytes)", copy.name, level.view(), size.view());

    const std::vector<std::vector<int>> nested{{1, 2}, {}, {3}};
    if (const auto status = xasrl::serialize(nested, bytes, context); status) {
        throw std::runtime_error(std::string{xasrl::message(status.code)});
    }
    const auto embedded_size = bytes.size();
    if (const auto status = xasrl::serialize<xasrl::schema_storage::schema_only>(nested, bytes, context); status) {
        throw std::runtime_error(std::string{xasrl::message(status.code)});
    }
    std::vector<std::vector<int>> restored;
    if (const auto status = xasrl::deserialize(bytes.span(), restored, context); status) {
        throw std::runtime_error(std::string{xasrl::message(status.code)});
    }
    std::println("Nested sequence: {} embedded bytes, {} schema-only bytes", embedded_size, bytes.size());

    PlayerProfileView moved;
    xasrl::transform_assign(moved, std::move(source));
    std::println("In-memory move: {}, rank {}, lives {}, health {}",
                 moved.display_name, moved.rank, moved.lives, moved.vitals.health);
}
