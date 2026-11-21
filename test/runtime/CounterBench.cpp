#include "BenchRecorder.h"

#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/CounterVocabulary.h>
#include <save/HeldRecordSaves.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <format>
#include <vector>

namespace
{
    Bench::Recorder Recorder;
    int Reps(int fallback) { return Bench::RepsFromEnvironment("SENCHA_COUNTER_BENCH_REPS", fallback); }

    double MicrosecondsSince(Bench::Clock::time_point start)
    {
        return std::chrono::duration<double, std::micro>(Bench::Clock::now() - start).count();
    }

    struct CounterBenchWorld
    {
        CounterBenchWorld()
            : Context(Logging)
        {
            ComponentRegistrar registrar(&Schema, &Serializers, nullptr);
            RegisterEngineComponents(registrar);
            Schema.Seal();
            Schema.Apply(Entities);
            Entities.AddResource<PersistentEntityIndex>();
            (void)InstallHeldRecords(Entities);
            Tags = &Entities.AddResource<GameplayTagRegistry>();
        }

        std::vector<GameplayTagId> Names(std::size_t count, std::string_view under)
        {
            std::vector<GameplayTagId> names;
            for (std::size_t i = 0; i < count; ++i)
                names.push_back(*Tags->RegisterTag(std::format("{}.{}", under, i)));
            return names;
        }

        LoggingProvider Logging;
        SceneSerializationContext Context;
        WorldComponentSchema Schema;
        ComponentSerializerRegistry Serializers;
        World Entities;
        GameplayTagRegistry* Tags = nullptr;
    };

    void RecordCounters(std::size_t count)
    {
        CounterBenchWorld bench;
        const std::vector<GameplayTagId> names = bench.Names(count, "flag");
        const GameplayTagId unset = *bench.Tags->RegisterTag("flag.never");

        auto start = Bench::Clock::now();
        for (const GameplayTagId name : names)
            (void)WriteCounter(bench.Entities, name, EntityId{}, 1);
        Recorder.Record(std::format("counters.{}.first_write", count), "us",
                        MicrosecondsSince(start) / static_cast<double>(count));
        ASSERT_EQ(bench.Entities.GetResource<HeldRecordIndex>().Count(), count);

        const int reps = Reps(20);
        std::int64_t sum = 0;
        start = Bench::Clock::now();
        for (int rep = 0; rep < reps; ++rep)
            for (const GameplayTagId name : names)
                sum += CounterValue(bench.Entities, name, EntityId{});
        Recorder.Record(std::format("counters.{}.value_set", count), "us",
                        MicrosecondsSince(start) / static_cast<double>(reps * count));
        ASSERT_EQ(sum, static_cast<std::int64_t>(reps * count));

        start = Bench::Clock::now();
        for (int rep = 0; rep < reps * static_cast<int>(count); ++rep)
            sum += CounterValue(bench.Entities, unset, EntityId{});
        Recorder.Record(std::format("counters.{}.value_unset", count), "us",
                        MicrosecondsSince(start) / static_cast<double>(reps * count));

        start = Bench::Clock::now();
        for (int rep = 0; rep < reps; ++rep)
        {
            (void)WriteCounter(bench.Entities, unset, EntityId{}, 1);
            (void)WriteCounter(bench.Entities, unset, EntityId{}, 0);
        }
        Recorder.Record(std::format("counters.{}.zero_crossing", count), "us",
                        MicrosecondsSince(start) / static_cast<double>(reps));

        std::vector<double> captures;
        for (int rep = 0; rep < Reps(5); ++rep)
        {
            start = Bench::Clock::now();
            const JsonValue saved = CaptureHeldRecords(bench.Entities, {}, bench.Serializers, bench.Context);
            captures.push_back(Bench::MillisecondsSince(start));
            ASSERT_EQ(saved.Find("records")->AsArray().size(), count);
        }
        Recorder.Record(std::format("counters.{}.capture", count), "ms", Bench::Median(captures));
        Recorder.Record(std::format("counters.{}.serializers", count), "count", static_cast<double>(bench.Serializers.Entries().size()));
    }
}

TEST(CounterBench, Generate)
{
    const char* out = std::getenv("SENCHA_COUNTER_BENCH_OUT");
    if (out == nullptr)
        GTEST_SKIP() << "set SENCHA_COUNTER_BENCH_OUT to record the counter bench";
    Recorder.Clear();
    for (const std::size_t count : { 50u, 500u })
        RecordCounters(count);
    const std::filesystem::path json(out);
    EXPECT_TRUE(Recorder.WriteJson(json));
    EXPECT_TRUE(Recorder.WriteCsv(std::filesystem::path(json).replace_extension(".csv")));
}
