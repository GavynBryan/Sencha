#pragma once

// A traversal harness: a chain of zones, a focus that walks it, and the real
// streaming path underneath — WorldPartitionRuntime demand, AsyncZoneLoader build
// and commit, RuntimeWorld residency processing — with the frame work a streaming
// event disturbs running every step.
//
// Shared because the same traversal is asserted two ways: counted work in
// StreamingTraversalTests.cpp, which holds on any machine, and wall clock in
// StreamingBench.Generate, which does not. A second copy of this fixture would
// eventually measure a different scenario than the one the bounds protect.
//
// The async lane defaults to no worker threads, which is its deterministic
// reference mode: every load lands in the frame that asked for it, so a lap is
// reproducible and the counted bounds are exact rather than timing-dependent.
// The determinism test drives the same lap at higher worker counts and settles
// in-flight work before each observation, which is the only way to compare
// laps whose loads land in different frames.

#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <render/PointLightComponent.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/PropagationOrderCache.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionRuntime.h>
#include <world/build/EntityBuildPackage.h>

#include "ZoneDockFixture.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace StreamingTraversal
{
// Zone shape: enough entities that a per-zone cost is visible against a per-world
// one, and a quarter of them parented so the propagation order is exercised rather
// than bypassed.
inline constexpr int kEntitiesPerZone = 400;
inline constexpr int kZoneCount = 8;
inline constexpr double kZoneSpan = 20.0;
inline constexpr int kFramesPerFocus = 4;

// Focus moves per lap: out along the chain and back, without repeating the end.
inline constexpr int kFocusMovesPerLap = kZoneCount * 2 - 1;
inline constexpr int kFramesPerLap = kFocusMovesPerLap * kFramesPerFocus;

inline ZoneId ZoneAt(int index)
{
    return ZoneId{ static_cast<std::uint64_t>(0xa0 + index) };
}

// A chain of zones along X, each connected to its neighbours, so walking the focus
// makes zones enter and leave the demand set continuously.
inline std::string ChainManifestJson()
{
    std::string json =
        R"({"format_version":1,"name":"Traversal","start_zone":"00000000000000a0",)"
        R"("regions":[{"id":"00000000000000b1","name":"Chain"}],"zones":[)";

    for (int index = 0; index < kZoneCount; ++index)
    {
        char buffer[512];
        const double minX = static_cast<double>(index) * kZoneSpan;
        std::snprintf(
            buffer,
            sizeof(buffer),
            R"({"id":"%016lx","name":"Zone%d","region":"00000000000000b1",)"
            R"("scene":"levels/z%d.sscene",)"
            R"("bounds":{"min":[%f,0,-8],"max":[%f,4,8]},)"
            R"("cooked_scene":"levels/z%d.smap",)"
            R"("content_hash":"%016lx"})",
            static_cast<unsigned long>(0xa0 + index),
            index,
            index,
            minX,
            minX + kZoneSpan,
            index,
            static_cast<unsigned long>(0xd0 + index));
        json += buffer;
        if (index + 1 < kZoneCount)
            json += ",";
    }

    // The chain's edges are added as cooked docks after parsing, so the JSON
    // carries only what a designer authors.
    json += "]}";
    return json;
}

inline WorldComponentSchema Schema()
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();
    return schema;
}

// One zone's worth of content, as a package a worker thread would have built.
inline void BuildZoneContent(EntityBuildPackage& package, int zoneIndex)
{
    std::vector<PackageEntityId> roots;
    for (int index = 0; index < kEntitiesPerZone; ++index)
    {
        const PackageEntityId entity = package.CreateEntity();

        Transform3f transform;
        transform.Position = Vec3d(
            static_cast<float>(zoneIndex) * static_cast<float>(kZoneSpan)
                + static_cast<float>(index % 16),
            0.0f,
            static_cast<float>(index / 16));
        package.AddComponent(entity, LocalTransform{ transform });

        PointLightComponent light{};
        light.Intensity = 1.0f;
        light.Range = 6.0f;
        package.AddComponent(entity, light);

        if (index % 4 == 0)
            roots.push_back(entity);
        else if (!roots.empty())
            package.SetParent(entity, roots.back());
    }
}

class Harness
{
public:
    // Worker count is the async lane's only timing knob. Zero is the
    // deterministic reference; a settled traversal must match it at any count.
    // `residentZoneCap` is the streaming budget. The default is the bench's,
    // and the counted bounds in StreamingTraversalTests depend on it. A test
    // with more than one focus needs its own: the cap bounds the merged demand
    // of every source at once, so two players in different parts of the chain
    // do not fit a budget sized for one.
    explicit Harness(unsigned taskThreads = 0, int residentZoneCap = 4)
        : TaskThreads_(taskThreads)
        , Tasks_(taskThreads)
        , Schema_(Schema())
        , World_(Schema_)
        , SceneContext_(Logging_)
        , Loader_(Tasks_, World_, Schema_, Serializers_, SceneContext_, FrameLoop_)
        , Partition_(MakeRecipe(), Config(residentZoneCap))
    {
        World_.Entities().AddResource<PropagationOrderCache>();
    }

    static WorldPartitionStreamingConfig Config(int residentZoneCap = 4)
    {
        WorldPartitionStreamingConfig config;
        config.HopCount = 1;         // focus plus immediate neighbours
        config.LingerSeconds = 0.0;  // unload as soon as demand drops
        config.ResidentZoneCap = residentZoneCap;
        return config;
    }

    // Empty on success, otherwise the reason. Returns a string rather than
    // asserting so the bench can use the same harness.
    std::string LoadManifest()
    {
        const std::optional<JsonValue> json = JsonParse(ChainManifestJson());
        if (!json.has_value())
            return "fixture manifest is not valid JSON";

        std::string error;
        const std::optional<WorldPartitionManifest> manifest =
            ReadWorldPartitionManifest(*json, &error);
        if (!manifest.has_value())
            return "manifest rejected: " + error;

        WorldPartitionManifest cooked = *manifest;
        for (int index = 0; index + 1 < kZoneCount; ++index)
            AddFixtureDockPair(cooked,
                               static_cast<std::uint64_t>(0xc0 + index),
                               ZoneAt(index), ZoneAt(index + 1),
                               Vec3d{ static_cast<float>(index + 1)
                                          * static_cast<float>(kZoneSpan),
                                      2.0f, 0.0f });
        if (!Partition_.LoadManifest(std::move(cooked), &error))
            return "manifest load failed: " + error;
        return {};
    }

    // One frame: streaming progresses, then the frame work a streaming event
    // disturbs runs over the domains the frame view produced.
    void StepFrame(double dt = 1.0 / 60.0)
    {
        // ::World, because the World() accessor below shadows the type name here.
        ::World& entities = World_.Entities();

        Partition_.Update(dt, Loader_, World_);
        World_.FlushLifecycleRequests();
        DrainOnce();
        World_.FlushLifecycleRequests();
        (void)World_.BeginResidencyProcessing();
        World_.FinalizeResidencyProcessing();

        const FrameZoneView& view = World_.BuildFrameView();
        PropagateTransforms(
            entities,
            view.Logic,
            TransformPropagationDomain::Simulation);
        World_.EndFrameView();
    }

    void SetFocusToZone(int index)
    {
        Partition_.SetFocus(Vec3d(
            static_cast<float>(index) * static_cast<float>(kZoneSpan) + 1.0f,
            1.0f,
            0.0f));
    }

    // Out along the chain and back, stepping frames as it goes, so zones attach
    // ahead of the focus and unload behind it.
    template <typename OnFrame>
    void WalkLap(OnFrame&& onFrame)
    {
        for (int index = 0; index < kZoneCount; ++index)
        {
            SetFocusToZone(index);
            for (int frame = 0; frame < kFramesPerFocus; ++frame)
                onFrame();
        }
        for (int index = kZoneCount - 2; index >= 0; --index)
        {
            SetFocusToZone(index);
            for (int frame = 0; frame < kFramesPerFocus; ++frame)
                onFrame();
        }
    }

    void WalkLap() { WalkLap([this] { StepFrame(); }); }

    // Pumps until nothing is in flight, so an observation taken afterwards
    // reflects streaming policy rather than how many workers happened to run.
    void SettleLoads()
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (AnyLoading())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                ADD_FAILURE() << "streaming did not settle within 10s";
                return;
            }
            DrainOnce();
            World_.FlushLifecycleRequests();
            (void)World_.BeginResidencyProcessing();
            World_.FinalizeResidencyProcessing();
        }
    }

    RuntimeWorld& World() { return World_; }
    WorldPartitionRuntime& Partition() { return Partition_; }
    const PropagationOrderCache& Cache()
    {
        return World_.Entities().GetResource<PropagationOrderCache>();
    }

    // Zones finalized so far. A traversal that streams nothing satisfies every
    // bound trivially, so callers state the churn they needed.
    int Attaches() const { return Attaches_; }

private:
    // PumpWork is the zero-worker path only; with workers the queue runs itself
    // and the owner thread just collects what finished.
    void DrainOnce()
    {
        if (TaskThreads_ == 0)
            Tasks_.PumpWork();
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Tasks_.DrainCompletions();
    }

    [[nodiscard]] bool AnyLoading() const
    {
        for (int index = 0; index < kZoneCount; ++index)
            if (Loader_.IsLoading(ZoneAt(index)))
                return true;
        return false;
    }

    ZoneLoadRecipeFn MakeRecipe()
    {
        return [this](const ZoneHeader& header)
        {
            const int index = static_cast<int>(header.Id.Value - 0xa0);
            ZoneLoadRecipe recipe;
            recipe.Build = [index](EntityBuildPackage& package)
            {
                BuildZoneContent(package, index);
            };
            recipe.Finalize = [this](RuntimeWorld&, RuntimeZoneRecord&)
            {
                ++Attaches_;
                return true;
            };
            return recipe;
        };
    }

    unsigned TaskThreads_ = 0;
    LoggingProvider Logging_;
    AsyncTaskQueue Tasks_;
    WorldComponentSchema Schema_;
    RuntimeWorld World_;
    ComponentSerializerRegistry Serializers_;
    SceneSerializationContext SceneContext_;
    RuntimeFrameLoop FrameLoop_;
    AsyncZoneLoader Loader_;
    WorldPartitionRuntime Partition_;
    int Attaches_ = 0;
};
} // namespace StreamingTraversal
