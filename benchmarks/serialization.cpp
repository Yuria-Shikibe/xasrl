#include <benchmark/benchmark.h>
#include <bitsery/adapter/buffer.h>
#include <bitsery/bitsery.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>
#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory_resource>
#include <span>
#include <spanstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "srl/core.hpp"
#include "srl/error.hpp"

namespace srl_bench {
    struct [[=srl::tag::auto_stream{}]] transform {
        float x{}, y{}, z{}, yaw{};
        bool operator==(const transform &) const = default;
    };

    struct [[=srl::tag::leaf{.allow_non_unique_presentation = true}]] leaf_transform {
        float x{}, y{}, z{}, yaw{};
        bool operator==(const leaf_transform &) const = default;
    };

    static_assert(sizeof(leaf_transform) == 16 && sizeof(leaf_transform) == sizeof(transform));
    static_assert(srl::leaf_uses_direct_object_representation<leaf_transform>());

    template<typename T>
    concept transform_type = std::same_as<T, transform> || std::same_as<T, leaf_transform>;

    constexpr std::uint32_t transform_batch_size = 4096;

    struct [[=srl::tag::auto_seq{}]] snapshot {
        std::uint32_t actor_id{}, flags{};
        transform pose;
        bool operator==(const snapshot &) const = default;
    };

    struct [[=srl::tag::auto_stream{}]] samples {
        std::vector<std::uint32_t> values;
        bool operator==(const samples &) const = default;
    };

    struct [[=srl::tag::auto_stream{}]] item {
        std::uint32_t item_id{};
        std::uint16_t count{}, durability{};
        bool operator==(const item &) const = default;
    };

    struct [[=srl::tag::auto_seq{}]] profile {
        std::uint32_t player_id{};
        std::string name;
        std::vector<item> inventory;
        bool operator==(const profile &) const = default;
    };

    struct [[=srl::tag::auto_stream{}]] entity {
        snapshot state;
        std::string name;
        std::vector<transform> path;
        bool operator==(const entity &) const = default;
    };

    struct [[=srl::tag::auto_seq{}]] scene {
        std::uint32_t revision{};
        std::vector<entity> entities;
        bool operator==(const scene &) const = default;
    };

    template<typename A>
    concept cereal_binary = std::same_as<A, cereal::BinaryInputArchive> || std::same_as<A, cereal::BinaryOutputArchive>;

    template<cereal_binary A, transform_type T> void serialize(A &ar, T &v) { ar(v.x, v.y, v.z, v.yaw); }
    template<cereal_binary A> void serialize(A &ar, snapshot &v) { ar(v.actor_id, v.flags, v.pose); }
    template<cereal_binary A> void serialize(A &ar, samples &v) { ar(v.values); }
    template<cereal_binary A> void serialize(A &ar, item &v) { ar(v.item_id, v.count, v.durability); }
    template<cereal_binary A> void serialize(A &ar, profile &v) { ar(v.player_id, v.name, v.inventory); }
    template<cereal_binary A> void serialize(A &ar, entity &v) { ar(v.state, v.name, v.path); }
    template<cereal_binary A> void serialize(A &ar, scene &v) { ar(v.revision, v.entities); }

    using bitsery_buffer = std::vector<std::uint8_t>;
    using bitsery_input = bitsery::InputBufferAdapter<bitsery_buffer>;
    using bitsery_output = bitsery::OutputBufferAdapter<bitsery_buffer>;

    template<typename A>
    concept bitsery_binary = std::same_as<A, bitsery::Serializer<bitsery_output>>
        || std::same_as<A, bitsery::Deserializer<bitsery_input>>;

    template<bitsery_binary A, transform_type T> void serialize(A &ar, T &v) {
        ar.value4b(v.x);
        ar.value4b(v.y);
        ar.value4b(v.z);
        ar.value4b(v.yaw);
    }

    template<bitsery_binary A, transform_type T> void serialize(A &ar, std::vector<T> &v) {
        ar.container(v, std::size_t{transform_batch_size});
    }

    template<bitsery_binary A> void serialize(A &ar, snapshot &v) {
        ar.value4b(v.actor_id);
        ar.value4b(v.flags);
        ar.object(v.pose);
    }

    template<bitsery_binary A> void serialize(A &ar, samples &v) { ar.container4b(v.values, 65536); }

    template<bitsery_binary A> void serialize(A &ar, item &v) {
        ar.value4b(v.item_id);
        ar.value2b(v.count);
        ar.value2b(v.durability);
    }

    template<bitsery_binary A> void serialize(A &ar, profile &v) {
        ar.value4b(v.player_id);
        ar.text1b(v.name, 128);
        ar.container(v.inventory, 256);
    }

    template<bitsery_binary A> void serialize(A &ar, entity &v) {
        ar.object(v.state);
        ar.text1b(v.name, 128);
        ar.container(v.path, 32);
    }

    template<bitsery_binary A> void serialize(A &ar, scene &v) {
        ar.value4b(v.revision);
        ar.container(v.entities, 4096);
    }

    snapshot make_snapshot() { return {42, 0x21, {17.25f, -8.5f, 3.75f, 90.0f}}; }

    template<transform_type T>
    T make_transform(std::uint32_t index) {
        return {17.25f + static_cast<float>(index), -8.5f + static_cast<float>(index % 7), 3.75f, 90.0f};
    }

    template<transform_type T>
    std::vector<T> make_transforms() {
        std::vector<T> result;
        result.reserve(transform_batch_size);
        for (std::uint32_t i = 0; i < transform_batch_size; ++i) {
            result.push_back(make_transform<T>(i));
        }
        return result;
    }

    samples make_samples(std::uint32_t count) {
        samples result{std::vector<std::uint32_t>(count)};
        for (std::uint32_t i = 0; i < count; ++i) {
            result.values[i] = i * 2654435761u;
        }
        return result;
    }

    profile make_profile() {
        profile result{7, "Player of the Northern Outpost", {}};
        for (std::uint16_t i = 0; i < 32; ++i) {
            result.inventory.push_back({1000u + 3u * i, static_cast<std::uint16_t>(1 + i % 5),
                                       static_cast<std::uint16_t>(100 - i)});
        }
        return result;
    }

    scene make_scene(std::uint32_t count) {
        scene result{3, {}};
        for (std::uint32_t i = 0; i < count; ++i) {
            auto state = make_snapshot();
            state.actor_id = i + 1;
            state.pose.x += static_cast<float>(i);
            entity actor{state, (i % 2 == 0 ? "NPC_" : "NorthernOutpostPatrol_") + std::to_string(i), {}};
            for (std::uint32_t j = 0; j < i % 9; ++j) {
                auto point = state.pose;
                point.y += static_cast<float>(j) * 2.0f;
                actor.path.push_back(point);
            }
            result.entities.push_back(std::move(actor));
        }
        return result;
    }

    void require_success(srl::error status) {
        if (status) {
            throw std::runtime_error(std::string{srl::message(status.code)});
        }
    }

    template<srl::schema_storage Mode>
    struct srl_codec {
        using buffer_type = srl::byte_buffer;
        const srl::serialization_context settings;
        srl::encoding_recorder recorder{*std::pmr::new_delete_resource()};

        std::size_t encode(const auto &value, buffer_type &bytes) {
            require_success(srl::serialize<Mode>(value, bytes, srl::srl_context{settings, recorder}));
            return bytes.size();
        }

        void decode(const buffer_type &bytes, std::size_t, auto &value) const {
            require_success(srl::deserialize(bytes.span(), value, settings));
        }
    };

    struct bitsery_codec {
        using buffer_type = bitsery_buffer;

        std::size_t encode(const auto &value, buffer_type &bytes) const {
            return bitsery::quickSerialization<bitsery_output>(bytes, value);
        }

        void decode(const buffer_type &bytes, std::size_t size, auto &value) const {
            const auto [error, consumed] = bitsery::quickDeserialization<bitsery_input>({bytes.cbegin(), size}, value);
            if (error != bitsery::ReaderError::NoError || !consumed) {
                throw std::runtime_error("Bitsery failed to decode the benchmark payload");
            }
        }
    };

    struct cereal_codec {
        using buffer_type = std::ostringstream;
        std::ispanstream input{std::span<char>{}};

        std::size_t encode(const auto &value, buffer_type &bytes) const {
            bytes.seekp(0);
            cereal::BinaryOutputArchive archive{bytes};
            archive(value);
            return static_cast<std::size_t>(bytes.tellp());
        }

        void decode(const buffer_type &bytes, std::size_t size, auto &value) {
            input.span(bytes.view().substr(0, size));
            cereal::BinaryInputArchive archive{input};
            archive(value);
            if (input.rdbuf()->in_avail() != 0) {
                throw std::runtime_error("cereal did not consume the benchmark payload");
            }
        }
    };

    template<bool Encode, typename Codec, typename T>
    void run_case(benchmark::State &state, const T &source, std::int64_t items) {
        Codec codec;
        typename Codec::buffer_type bytes;
        const auto wire_size = codec.encode(source, bytes);
        {
            T restored{};
            codec.decode(bytes, wire_size, restored);
            if (restored != source) {
                throw std::runtime_error("benchmark round trip changed the source object");
            }
        }

        // Keep fixed fixtures and their encoded bytes opaque to the optimizer.
        benchmark::DoNotOptimize(&source);
        benchmark::DoNotOptimize(&bytes);
        for (auto _ : state) {
            if constexpr (Encode) {
                benchmark::DoNotOptimize(codec.encode(source, bytes));
                benchmark::DoNotOptimize(&bytes);
                benchmark::ClobberMemory();
            } else {
                T restored{};
                codec.decode(bytes, wire_size, restored);
                benchmark::DoNotOptimize(&restored);
                benchmark::ClobberMemory();
            }
        }
        state.counters["wire_bytes"] = static_cast<double>(wire_size);
        state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(wire_size));
        state.SetItemsProcessed(state.iterations() * items);
    }

    template<typename Codec, typename Make>
    void register_codec(std::string_view workload, std::string_view codec, Make make, std::int64_t items) {
        const auto suffix = std::string{workload} + "/" + std::string{codec};
        benchmark::RegisterBenchmark("Encode/" + suffix, [=](benchmark::State &state) {
            run_case<true, Codec>(state, make(), items);
        });
        benchmark::RegisterBenchmark("Decode/" + suffix, [=](benchmark::State &state) {
            run_case<false, Codec>(state, make(), items);
        });
    }

    template<typename Make>
    void register_workload(std::string_view name, Make make, std::int64_t items) {
        register_codec<srl_codec<srl::schema_storage::embedded>>(name, "SRL_Embedded", make, items);
        register_codec<srl_codec<srl::schema_storage::schema_only>>(name, "SRL_SchemaOnly", make, items);
        register_codec<bitsery_codec>(name, "Bitsery", make, items);
        register_codec<cereal_codec>(name, "Cereal", make, items);
    }
}

int main(int argc, char **argv) try {
    using namespace srl_bench;
    register_workload("Snapshot", make_snapshot, 1);
    register_workload("Samples/1024", [] { return make_samples(1024); }, 1024);
    register_workload("Samples/65536", [] { return make_samples(65536); }, 65536);
    register_workload("Profile", make_profile, 1);
    register_workload("Scene/128", [] { return make_scene(128); }, 128);
    register_workload("Scene/1024", [] { return make_scene(1024); }, 1024);
    register_workload("Transform/Single/Stream", [] { return make_transform<transform>(0); }, 1);
    register_workload("Transform/Single/Leaf", [] { return make_transform<leaf_transform>(0); }, 1);
    register_workload("Transform/Batch4096/Stream", make_transforms<transform>, transform_batch_size);
    register_workload("Transform/Batch4096/Leaf", make_transforms<leaf_transform>, transform_batch_size);

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
} catch (const std::exception &error) {
    std::fprintf(stderr, "serialization benchmark: %s\n", error.what());
    return 1;
}
