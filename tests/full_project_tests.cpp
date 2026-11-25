#include "srl/core.hpp"
#include "srl/diagnostics.hpp"
#include "srl/io.hpp"
#include "srl/number_to_string.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory_resource>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace full_project_tests {
    struct [[=xasrl::tag::leaf{}]] position {
        std::uint32_t x{};
        std::uint32_t y{};
    };

    enum class state : std::uint8_t { idle = 1, active = 4 };

    struct [[=xasrl::tag::auto_stream{}]] event {
        state status{state::idle};
        std::string name;
        std::vector<position> trail;
    };

    struct save {
        [[=xasrl::tag::field{1}]] std::uint32_t revision{};
        [[=xasrl::tag::field{2}]] std::vector<event> events;
        [[=xasrl::tag::field{3}]] bool enabled{};
        int runtime_cookie{};
    };

    struct save_view {
        [[=xasrl::tag::field{1}]] std::uint32_t revision{};
        [[=xasrl::tag::field{3}]] bool enabled{};
    };

    static_assert(xasrl::schema_valid(^^save) && xasrl::schema_valid(^^save_view));
    static_assert(xasrl::serialized_field_count(^^save) == 3);

    [[nodiscard]] save sample() {
        return {7, {{state::active, "start", {{2, 3}, {5, 8}}},
                    {state::idle, "", {}}}, true, 41};
    }

    void expect_sample(const save &result, int cookie) {
        EXPECT_EQ(result.revision, 7u);
        EXPECT_TRUE(result.enabled);
        EXPECT_EQ(result.runtime_cookie, cookie);
        ASSERT_EQ(result.events.size(), 2u);
        EXPECT_EQ(result.events[0].status, state::active);
        EXPECT_EQ(result.events[0].name, "start");
        ASSERT_EQ(result.events[0].trail.size(), 2u);
        EXPECT_EQ(result.events[0].trail[0].x, 2u);
        EXPECT_EQ(result.events[0].trail[0].y, 3u);
        EXPECT_EQ(result.events[0].trail[1].x, 5u);
        EXPECT_EQ(result.events[0].trail[1].y, 8u);
        EXPECT_EQ(result.events[1].status, state::idle);
        EXPECT_TRUE(result.events[1].name.empty());
        EXPECT_TRUE(result.events[1].trail.empty());
    }

    TEST(FullProject, PreparedAndDirectEncodingAgreeAcrossRecorderReuse) {
        const xasrl::serialization_context context;
        const auto source = sample();
        const save empty{};
        xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
        xasrl::byte_buffer direct;
        std::vector<std::byte> output;

        for (const auto *input : {&source, &empty, &source}) {
            {
                const auto measured = xasrl::serialized_size(recorder, *input, context);
                ASSERT_TRUE(measured);
                auto prepared = xasrl::prepared_serialization<save>::prepare(recorder, *input, context);
                ASSERT_TRUE(prepared);
                ASSERT_EQ(prepared->size(), *measured);
                output.resize(prepared->size());
                ASSERT_FALSE(prepared->encode(std::span{output}));
            }
            ASSERT_FALSE(xasrl::serialize(*input, direct, xasrl::srl_context{context, recorder}));
            EXPECT_TRUE(std::ranges::equal(output, direct.span()));
        }

        save result{.runtime_cookie = 99};
        ASSERT_FALSE(xasrl::deserialize(std::span{output}, result, context));
        expect_sample(result, 99);
    }

    TEST(FullProject, BlockRoundTripSupportsProjectionAndByteBudget) {
        xasrl::serialization_context context;
        const auto source = sample();
        xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(source, bytes, xasrl::srl_context{context, recorder}));

        const auto pid = xasrl::number_to_string<10, wchar_t>(GetCurrentProcessId());
        const auto path = std::filesystem::temp_directory_path()
            / std::wstring{L"srl_full_project_"}.append(pid.view()).append(L".bin");
        struct remove_test_file {
            const std::filesystem::path &path;
            ~remove_test_file() {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        } cleanup{path};

        ASSERT_FALSE(xasrl::write_block(path, bytes.span()));
        const auto over_budget = xasrl::read_block(path, bytes.size() - 1);
        ASSERT_FALSE(over_budget);
        EXPECT_EQ(over_budget.error().code, xasrl::error_code::resource_limit_exceeded);
        const auto loaded = xasrl::read_block(path, bytes.size());
        ASSERT_TRUE(loaded);
        EXPECT_EQ(*loaded, bytes);
        context.config.limits.max_input_bytes = loaded->size();

        save_view projection{};
        ASSERT_FALSE(xasrl::deserialize(loaded->span(), projection, context));
        EXPECT_EQ(projection.revision, 7u);
        EXPECT_TRUE(projection.enabled);

        save result{.runtime_cookie = 17};
        ASSERT_FALSE(xasrl::deserialize(loaded->span(), result, context));
        expect_sample(result, 17);
    }

    TEST(FullProject, EveryTruncatedPrefixIsRejectedWithoutChangingTheTarget) {
        const xasrl::serialization_context context;
        const auto source = sample();
        xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
        xasrl::byte_buffer bytes;
        ASSERT_FALSE(xasrl::serialize(source, bytes, xasrl::srl_context{context, recorder}));
        const auto seed = save{.revision = 100, .events = {{state::idle, "retained", {{9, 10}}}},
                               .enabled = false, .runtime_cookie = 99};
        auto target = seed;
        xasrl::byte_buffer before, after;
        ASSERT_FALSE(xasrl::serialize(seed, before, xasrl::srl_context{context, recorder}));

        for (std::size_t length = 0; length < bytes.size(); ++length) {
            SCOPED_TRACE(length);
            const auto failure = xasrl::deserialize(bytes.span().first(length), target, context);
            ASSERT_TRUE(failure);
            EXPECT_FALSE(xasrl::format_error(failure).empty());
            ASSERT_FALSE(xasrl::serialize(target, after, xasrl::srl_context{context, recorder}));
            ASSERT_EQ(after, before);
            EXPECT_EQ(target.runtime_cookie, seed.runtime_cookie);
        }

        ASSERT_FALSE(xasrl::deserialize(bytes.span(), target, context));
        expect_sample(target, 99);
    }
}
