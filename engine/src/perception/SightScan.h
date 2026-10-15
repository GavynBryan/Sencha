#pragma once

#include <perception/PerceptionSystem.h>

#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagId.h>
#include <spatial/sight/SightComponents.h>
#include <world/transform/TransformComponents.h>

#include <optional>
#include <vector>

class PerceptionFrame;
class PhysicsWorld;

struct SightScanSettings
{
    GameplayTagId Sense;
    std::uint32_t RaysPerObserver = 32;
    bool Trace = false;
    // Valid: trace only this observer.
    EntityId TracedObserver;
};

// Every SightSensor against every SightTarget in the given partitions, in
// ascending id order on both sides. Owner thread, FixedLogic only.
class SightScan
{
public:
    explicit SightScan(const PhysicsWorld& physics);

    void Run(const World& world, const StoragePartitionSet& partitions, const SightScanSettings& settings,
             PerceptionFrame& frame, SightStats& stats, std::vector<SightTraceRow>& trace);

    [[nodiscard]] std::span<const EntityId> Observers() const { return ObserverIds; }

    struct PlacedObserver
    {
        EntityId Entity;
        std::optional<SightObserver> Eye;
    };
    [[nodiscard]] std::span<const PlacedObserver> PlacedObservers() const { return Placed; }

private:
    struct PlacedTarget
    {
        EntityId Entity;
        Vec3d Sample;
    };

    // Where an observer's last capped scan stopped, so the next one resumes
    // after it instead of spending its rays on the same lowest ids again.
    struct RayCursor
    {
        EntityId Observer;
        EntityId LastTested;
    };

    void Gather(const World& world, const StoragePartitionSet& partitions);
    void GrantRays(EntityId observer, std::uint32_t raysPerObserver);

    const PhysicsWorld& Physics;
    const World* QueriedWorld = nullptr;
    std::optional<Query<Read<WorldTransform>, Read<SightSensor>>> Sensors;
    std::optional<Query<Read<WorldTransform>, Read<SightTarget>>> Targets;
    std::vector<PlacedObserver> Placed;
    std::vector<PlacedTarget> PlacedTargets;
    std::vector<EntityId> ObserverIds;
    std::vector<SightOutcome> Geometry;
    std::vector<std::uint32_t> InView;
    std::vector<std::uint8_t> Granted;
    std::vector<RayCursor> Cursors;
    std::vector<RayCursor> NextCursors;
};
