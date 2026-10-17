#pragma once

#include <app/GameContexts.h>
#include <navigation/NavPlanQueue.h>

#include <cstddef>
#include <memory>
#include <span>

class ConsoleRegistry;
class DataAssetCache;
class JobSystem;
class RuntimeWorld;

// What the follower holds for one agent, for a debug view.
struct NavAgentInspection
{
    bool HasRoute = false;
    bool Queued = false;
    bool Crossing = false;
    std::uint32_t Step = 0;
    std::uint32_t StepCount = 0;
    std::uint32_t Corner = 0;
    std::uint32_t LegCorners = 0;
    NavLinkId Link;
    std::uint32_t CrossingTicks = 0;
    std::uint8_t CrossingFailures = 0;
    std::uint8_t StuckReplans = 0;
    float StuckElapsed = 0.0f;
    float StuckExpected = 0.0f;
    NavQueryDiagnostics LastSearch;
};

// Drives every NavAgent with an order: plans routes in one fork-join batch a
// tick, follows them by writing MovementIntent, and reports in NavAgentProgress.
// Authority only. See docs/navigation/runtime.md, "Agents".
class NavAgentSystem
{
public:
    struct Services
    {
        RuntimeWorld& Runtime;
        JobSystem* Jobs = nullptr;
        const DataAssetCache* DataAssets = nullptr;
        ConsoleRegistry* Console = nullptr;
    };

    explicit NavAgentSystem(Services services);
    ~NavAgentSystem();
    NavAgentSystem(const NavAgentSystem&) = delete;
    NavAgentSystem& operator=(const NavAgentSystem&) = delete;

    void FixedLogic(FixedLogicContext& ctx);

    struct Stats
    {
        std::size_t Agents = 0;
        std::size_t Active = 0;
        std::size_t Planned = 0;
        std::size_t Queued = 0;
        std::size_t Repaths = 0;
        std::size_t Failures = 0;
        std::size_t AgentsWiderThanProfile = 0;
        bool PlannedInParallel = false;
    };
    [[nodiscard]] const Stats& LastStats() const;

    // Debug inspection: the agents the last tick processed, in id order, and
    // the one nav.agent.debug.draw and the panel single out.
    [[nodiscard]] std::span<const EntityId> LastAgents() const;
    [[nodiscard]] bool Inspect(EntityId agent, NavAgentInspection& out) const;
    void SelectAgent(EntityId agent);
    [[nodiscard]] EntityId SelectedAgent() const;
    [[nodiscard]] const NavPlanQueue& Queue() const;
    [[nodiscard]] std::size_t RouteSlotsInUse() const;
    // Contexts the planner has made: one, or one per worker plus the caller.
    [[nodiscard]] std::size_t PlanningContexts() const;

private:
    struct State;
    std::unique_ptr<State> Impl;
};
