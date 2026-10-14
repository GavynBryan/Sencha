#pragma once

#include <app/GameContexts.h>
#include <spatial/sight/SightTest.h>

#include <cstdint>
#include <memory>
#include <span>

class ConsoleRegistry;
class PhysicsWorld;

// What the last tick's sight scan spent and why each candidate went the way it
// did. A candidate is one observer-target pair.
struct SightStats
{
    std::uint32_t Sensors = 0;
    std::uint32_t UnusableSensors = 0;
    std::uint32_t Targets = 0;
    std::uint32_t Candidates = 0;
    std::uint32_t OutOfRange = 0;
    std::uint32_t OutsideCone = 0;
    std::uint32_t Blocked = 0;
    std::uint32_t Seen = 0;
    std::uint32_t Untested = 0;
    std::uint32_t Raycasts = 0;
    std::uint32_t PartialScans = 0;
};

// One candidate's fate, kept while debug drawing or the debug panel asks.
struct SightTraceRow
{
    EntityId Observer;
    EntityId Target;
    Vec3d Eye = Vec3d::Zero();
    Vec3d Sample = Vec3d::Zero();
    SightOutcome Outcome = SightOutcome::Untested;
    // Where the ray stopped, for Blocked rows.
    Vec3d HitPoint = Vec3d::Zero();
};

// Runs every sense each fixed tick on the authority and publishes the World's
// PerceptionFrame. Readers in FixedLogic order themselves after it.
class PerceptionSystem
{
public:
    PerceptionSystem(const PhysicsWorld& physics, ConsoleRegistry* console);
    ~PerceptionSystem();

    PerceptionSystem(const PerceptionSystem&) = delete;
    PerceptionSystem& operator=(const PerceptionSystem&) = delete;

    void FixedLogic(FixedLogicContext& ctx);

    [[nodiscard]] const SightStats& LastSightStats() const;

    // Debug inspection. The trace is kept only while drawing is on or an
    // observer is selected; with a selection it holds that observer's rows.
    void SelectObserver(EntityId observer);
    [[nodiscard]] EntityId SelectedObserver() const;
    [[nodiscard]] std::span<const SightTraceRow> LastTrace() const;
    [[nodiscard]] std::span<const EntityId> LastObservers() const;

private:
    struct State;
    std::unique_ptr<State> Impl;
};
