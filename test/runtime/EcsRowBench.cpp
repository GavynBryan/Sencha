// Records what row size costs an ECS query and a structural change, over the real
// World: the sweeps a system makes, and the moves a component add or remove makes.
// Env-gated; set SENCHA_ECS_BENCH_OUT, from the profile preset.

#include "BenchRecorder.h"

#include <ecs/Ecs.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <vector>

namespace
{
    struct RowBenchHot
    {
        float Values[10] = {};
    };

    struct RowBenchFlag
    {
        int Value = 0;
    };

    template <std::size_t Bytes>
    struct RowBenchPayload
    {
        char Data[Bytes] = {};
    };

    using Payload16K = RowBenchPayload<16300>;
    using Payload8K = RowBenchPayload<8000>;
    using Payload2K = RowBenchPayload<2000>;
    using Payload200 = RowBenchPayload<200>;
}

SENCHA_DECLARE_COMPONENT_TYPE(RowBenchHot, "bench.row_hot");
SENCHA_DECLARE_COMPONENT_TYPE(RowBenchFlag, "bench.row_flag");
SENCHA_DECLARE_COMPONENT_TYPE(Payload16K, "bench.row_16k");
SENCHA_DECLARE_COMPONENT_TYPE(Payload8K, "bench.row_8k");
SENCHA_DECLARE_COMPONENT_TYPE(Payload2K, "bench.row_2k");
SENCHA_DECLARE_COMPONENT_TYPE(Payload200, "bench.row_200");

namespace
{
    template <typename Payload>
    void Measure(Bench::Recorder& recorder, std::string_view label, int count)
    {
        World world;
        world.RegisterComponent<RowBenchHot>();
        world.RegisterComponent<RowBenchFlag>();
        world.RegisterComponent<Payload>();
        std::vector<EntityId> entities;
        for (int i = 0; i < count; ++i)
        {
            const EntityId entity = world.CreateEntity();
            world.AddComponent<RowBenchHot>(entity);
            world.AddComponent<Payload>(entity);
            entities.push_back(entity);
        }

        Query<Read<RowBenchHot>> query(world);
        double sum = 0.0;
        std::vector<double> sweeps;
        for (int rep = 0; rep < 64; ++rep)
        {
            const auto start = Bench::Clock::now();
            for (int sweep = 0; sweep < 256; ++sweep)
                query.ForEachChunk([&](auto& view) {
                    const auto hot = view.template Read<RowBenchHot>();
                    for (std::uint32_t i = 0; i < view.Count(); ++i)
                        sum += hot[i].Values[3];
                });
            sweeps.push_back(Bench::MillisecondsSince(start) * 1.0e6 / (256.0 * count));
        }
        std::vector<double> moves;
        for (int rep = 0; rep < 24; ++rep)
        {
            const auto start = Bench::Clock::now();
            for (const EntityId entity : entities)
                world.AddComponent<RowBenchFlag>(entity);
            for (const EntityId entity : entities)
                world.RemoveComponent<RowBenchFlag>(entity);
            moves.push_back(Bench::MillisecondsSince(start) * 1.0e6 / (2.0 * count));
        }
        EXPECT_EQ(sum, 0.0);
        recorder.Record(std::format("ecs.sweep.{}.entities_{}", label, count), "ns", Bench::Median(sweeps));
        recorder.Record(std::format("ecs.move.{}.entities_{}", label, count), "ns", Bench::Median(moves));
    }
}

TEST(EcsRowBench, Generate)
{
    const char* out = std::getenv("SENCHA_ECS_BENCH_OUT");
    if (out == nullptr)
        GTEST_SKIP() << "set SENCHA_ECS_BENCH_OUT to record the row-size bench";
    const std::filesystem::path json(out);
    if (json.has_parent_path())
        std::filesystem::create_directories(json.parent_path());

    Bench::Recorder recorder;
    for (const int count : { 256, 4096 })
    {
        Measure<Payload16K>(recorder, "row_16k", count);
        Measure<Payload8K>(recorder, "row_8k", count);
        Measure<Payload2K>(recorder, "row_2k", count);
        Measure<Payload200>(recorder, "row_200", count);
    }
    ASSERT_TRUE(recorder.WriteJson(json)) << "cannot write " << json.generic_string();
}
