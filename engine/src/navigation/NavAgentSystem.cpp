#include <navigation/NavAgentSystem.h>

#include "NavPolicyBindings.h"
#include "NavRouteSlots.h"

#include <core/console/CVarRead.h>
#include <core/console/ConsoleRegistry.h>
#include <debug/DebugLines.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <jobs/JobSystem.h>
#include <movement/MovementIntent.h>
#include <movement/MovementTags.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/MotionChannels.h>
#include <movement/components/MovementTuning.h>
#include <navigation/NavAgent.h>
#include <navigation/NavRouteFollowing.h>
#include <navigation/NavRoutePlanning.h>
#include <navigation/NavTileMesh.h>
#include <navigation/NavTraversal.h>
#include <navigation/NavigationQuery.h>
#include <navigation/ZoneNavigation.h>
#include <physics/components/CharacterController.h>
#include <time/ActorTime.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace
{
    struct CVarSpec
    {
        std::string_view Name;
        double Default;
        double Min;
        double Max;
        std::string_view Help;
    };

    // The follower's tunables, in the order NavAgentSettings reads them.
    constexpr CVarSpec kCVars[] = {
        { "nav.agent.plans_per_tick", 16, 1, 1024, "Route searches the navigation agents may run in one fixed tick." },
        { "nav.agent.parallel_min_batch", 2, 1, 1024, "Smallest batch of route searches worth spreading over the job workers." },
        { "nav.agent.max_nodes", 4096, 16, 65535, "Search nodes one agent route search may use; read when the planner is made." },
        { "nav.agent.corner_radius", 0.3, 0.01, 10, "Metres within which an agent counts a route corner as reached." },
        { "nav.agent.arrive_radius", 0.5, 0.01, 100, "Default metres within which an agent has arrived." },
        { "nav.agent.deviation", 1.5, 0.1, 100, "Metres off its route beyond which an agent stops and replans." },
        { "nav.agent.retarget_distance", 1.0, 0.01, 1000, "Metres a followed target must move before the route is refreshed." },
        { "nav.agent.retarget_fraction", 0.25, 0, 10, "Fraction of the remaining route a followed target must move before a refresh." },
        { "nav.agent.repath_interval", 0.5, 0, 60, "Seconds between refreshes for a moving target." },
        { "nav.agent.refresh_defer_ticks", 8, 0, 600, "Ticks a refresh yields to agents that are standing still." },
        { "nav.agent.stuck_window", 1.0, 0.05, 60, "Agent seconds over which progress is measured." },
        { "nav.agent.stuck_progress", 0.25, 0, 1, "Fraction of the asked-for progress below which a window is stuck." },
        { "nav.agent.stuck_repaths", 2, 0, 100, "Consecutive stuck replans before a request fails as stuck." },
        { "nav.agent.avoid_seconds", 5.0, 0, 600, "Seconds an agent avoids a link it failed to cross." },
        { "nav.agent.traversal_timeout", 5.0, 0.1, 600, "Agent seconds a link crossing may take before it counts as failed." },
        { "nav.agent.offmesh_grace", 0.5, 0, 60, "Seconds an agent may stand off the navigation mesh before its request fails." },
        { "nav.agent.follow_hysteresis", 1.5, 1, 100, "How far past its arrive radius a followed target may get before the agent moves again." },
    };

    struct NavAgentSettings
    {
        std::size_t PlansPerTick = 16;
        std::size_t ParallelMinBatch = 2;
        float CornerRadius = 0.3f;
        float ArriveRadius = 0.5f;
        float Deviation = 1.5f;
        float RetargetDistance = 1.0f;
        float RetargetFraction = 0.25f;
        float RepathInterval = 0.5f;
        std::uint32_t RefreshDeferTicks = 8;
        NavStuckRule Stuck;
        std::uint32_t StuckRepaths = 2;
        float AvoidSeconds = 5.0f;
        float TraversalTimeout = 5.0f;
        float OffMeshGrace = 0.5f;
        float FollowHysteresis = 1.5f;
    };

    double ReadAgentCVar(const ConsoleRegistry* console, std::size_t index)
    {
        return ReadCVarDouble(console, kCVars[index].Name, kCVars[index].Default);
    }

    NavAgentSettings ReadSettings(const ConsoleRegistry* console)
    {
        NavAgentSettings s;
        s.PlansPerTick = static_cast<std::size_t>(std::max(1.0, ReadAgentCVar(console, 0)));
        s.ParallelMinBatch = static_cast<std::size_t>(std::max(1.0, ReadAgentCVar(console, 1)));
        s.CornerRadius = static_cast<float>(ReadAgentCVar(console, 3));
        s.ArriveRadius = static_cast<float>(ReadAgentCVar(console, 4));
        s.Deviation = static_cast<float>(ReadAgentCVar(console, 5));
        s.RetargetDistance = static_cast<float>(ReadAgentCVar(console, 6));
        s.RetargetFraction = static_cast<float>(ReadAgentCVar(console, 7));
        s.RepathInterval = static_cast<float>(ReadAgentCVar(console, 8));
        s.RefreshDeferTicks = static_cast<std::uint32_t>(ReadAgentCVar(console, 9));
        s.Stuck.WindowSeconds = static_cast<float>(ReadAgentCVar(console, 10));
        s.Stuck.MinimumProgress = static_cast<float>(ReadAgentCVar(console, 11));
        s.StuckRepaths = static_cast<std::uint32_t>(ReadAgentCVar(console, 12));
        s.AvoidSeconds = static_cast<float>(ReadAgentCVar(console, 13));
        s.TraversalTimeout = static_cast<float>(ReadAgentCVar(console, 14));
        s.OffMeshGrace = static_cast<float>(ReadAgentCVar(console, 15));
        s.FollowHysteresis = static_cast<float>(ReadAgentCVar(console, 16));
        return s;
    }

    constexpr std::string_view kDebugDrawCVar = "nav.agent.debug.draw";
    constexpr std::string_view kDebugChannel = "navigation.agents";
    const Vec4 kRouteColor{ 0.25f, 0.85f, 0.45f, 1.0f };
    const Vec4 kCrossingColor{ 0.95f, 0.6f, 0.15f, 1.0f };
    const Vec4 kWaitingColor{ 0.6f, 0.6f, 0.6f, 1.0f };
    const Vec4 kSelectedColor{ 1.0f, 1.0f, 0.3f, 1.0f };
    constexpr float kMarkerSize = 0.3f;
    constexpr float kLift = 0.05f;

    void RegisterCVars(ConsoleRegistry& console)
    {
        console.RegisterCVar({
            .Name = std::string(kDebugDrawCVar),
            .Owner = "navigation",
            .Type = CVarType::Bool,
            .DefaultValue = false,
            .CurrentValue = false,
            .Help = "Draws every navigation agent's remaining route and destination, and the selected "
                    "agent's over the scene with its wish direction.",
            .Source = { "navigation" },
        });
        for (const CVarSpec& spec : kCVars)
            console.RegisterCVar({
                .Name = std::string(spec.Name),
                .Owner = "navigation",
                .Type = CVarType::Double,
                .DefaultValue = spec.Default,
                .CurrentValue = spec.Default,
                .Help = std::string(spec.Help),
                .Source = { "navigation" },
                .Min = spec.Min,
                .Max = spec.Max,
            });
    }

    bool IsActivePhase(NavAgentPhase phase)
    {
        return phase == NavAgentPhase::Planning || phase == NavAgentPhase::Moving
            || phase == NavAgentPhase::Traversing || phase == NavAgentPhase::Holding;
    }

    NavAgentFailure FailureForSearchStatus(NavStatus status, bool aroundFailedCrossing)
    {
        switch (status)
        {
        case NavStatus::NoPath: return aroundFailedCrossing ? NavAgentFailure::TraversalFailed : NavAgentFailure::NoPath;
        case NavStatus::InvalidStart: return NavAgentFailure::StartOffNavigation;
        case NavStatus::InvalidDestination: return NavAgentFailure::DestinationOffNavigation;
        case NavStatus::ProfileUnavailable: return NavAgentFailure::ProfileUnavailable;
        case NavStatus::CrossZoneUnsupported: return NavAgentFailure::DestinationInOtherZone;
        case NavStatus::SearchLimitReached: return NavAgentFailure::SearchLimitReached;
        case NavStatus::OutputCapacityReached: return NavAgentFailure::RouteTooLong;
        default: return NavAgentFailure::ZoneUnavailable;
        }
    }

    NavRepathReason RepathReasonForValidity(NavRouteValidity validity)
    {
        switch (validity)
        {
        case NavRouteValidity::TileChanged: return NavRepathReason::TileChanged;
        case NavRouteValidity::LinkChanged: return NavRepathReason::LinkChanged;
        default: return NavRepathReason::ZoneReloaded;
        }
    }

    // Route distance left from `position`, current leg first.
    float RouteRemaining(const NavRouteBuffer& route, std::uint32_t stepIndex, std::uint32_t corner,
                         const Vec3d& position)
    {
        const auto steps = route.Steps();
        const auto corners = route.Corners();
        float remaining = 0.0f;
        Vec3d last = position;
        for (std::uint32_t s = stepIndex; s < steps.size(); ++s)
        {
            const NavRouteStep& step = steps[s];
            if (step.Kind == NavRouteStepKind::Walk)
            {
                const auto leg = corners.subspan(step.FirstCorner, step.CornerCount);
                const std::uint32_t from = s == stepIndex ? corner : 0;
                remaining += NavRemainingOnLeg(leg, from, last);
                if (!leg.empty())
                    last = leg.back();
            }
            else
            {
                remaining += NavPlanarDistance(last, step.Entry) + NavPlanarDistance(step.Entry, step.Exit);
                last = step.Exit;
            }
        }
        return remaining;
    }
}

struct NavAgentSystem::State
{
    explicit State(Services services)
        : Runtime(services.Runtime)
        , Jobs(services.Jobs)
        , Console(services.Console)
        , Policies(services.DataAssets)
        , Planner(services.Jobs,
                  NavQueryContextConfig{ static_cast<std::uint32_t>(ReadAgentCVar(services.Console, 2)), 512 },
                  NavRouteCapacity{})
        , Slots(NavRouteCapacity{})
    {
    }

    RuntimeWorld& Runtime;
    JobSystem* Jobs = nullptr;
    ConsoleRegistry* Console = nullptr;
    NavPolicyBindings Policies;
    NavRoutePlanner Planner;
    NavRouteSlots Slots;
    NavPlanQueue Queue;
    std::optional<Query<Read<NavAgent>>> Agents;
    std::uint64_t StructuralVersion = ~std::uint64_t{ 0 };

    NavAgentSettings Settings;
    Stats LastStats;
    std::uint64_t Tick = 0;
    float DeltaSeconds = 0.0f;
    const StoragePartitionSet* Partitions = nullptr;
    std::vector<EntityId> Active;
    std::vector<NavPlanEntry> Admitted;
    std::vector<NavPlanInput> Inputs;
    std::vector<EntityId> InputAgents;
    EntityId Selected;
    bool Drawing = false;

    // -- Shared helpers -------------------------------------------------------

    World& Entities() { return Runtime.Entities(); }

    static void ZeroIntent(World& world, EntityId agent)
    {
        if (world.IsRegistered<MovementIntent>())
            if (MovementIntent* intent = world.TryGet<MovementIntent>(agent))
                *intent = MovementIntent{};
    }

    bool Controlled(const World& world, EntityId agent) const
    {
        const MovementTags* tags = world.TryGetResource<MovementTags>();
        if (tags == nullptr || !tags->Controlled.IsValid() || !world.IsRegistered<GameplayTagContainer>())
            return false;
        const GameplayTagContainer* held = world.TryGet<GameplayTagContainer>(agent);
        return held != nullptr && held->HasExact(tags->Controlled);
    }

    NavRouteSlot* SlotOf(EntityId agent, const NavAgentProgress& progress)
    {
        return Slots.Owned(progress.RouteSlot, agent);
    }

    std::uint64_t TicksFor(float seconds) const
    {
        return DeltaSeconds > 0.0f ? static_cast<std::uint64_t>(std::ceil(seconds / DeltaSeconds)) : 0;
    }

    void End(World& world, EntityId agent, NavAgentProgress& progress, NavAgentPhase phase,
             NavAgentFailure failure, bool clearIntent = true)
    {
        AbortCrossing(world, agent, progress);
        progress.Phase = phase;
        progress.Failure = failure;
        if (phase == NavAgentPhase::Failed)
            ++LastStats.Failures;
        Slots.Release(progress.RouteSlot);
        progress.RouteSlot = kNavNoRouteSlot;
        Queue.Remove(agent);
        if (clearIntent)
            ZeroIntent(world, agent);
    }

    void Replan(World& world, EntityId agent, NavAgentProgress& progress, NavRouteSlot& slot,
                NavRepathReason reason)
    {
        ZeroIntent(world, agent);
        slot.HasRoute = false;
        slot.Stuck = NavStuckWindow{};
        progress.Phase = NavAgentPhase::Planning;
        progress.LastRepath = reason;
        ++progress.Repaths;
        ++LastStats.Repaths;
        Queue.Queue(agent, progress.Request, NavPlanClass::Blocking, Tick, Settings.RefreshDeferTicks);
    }

    std::optional<Vec3d> TargetPosition(const World& world, const NavAgentRequest& order) const
    {
        if (order.Kind == NavDestinationKind::Point)
            return order.Point;
        if (!world.IsAlive(order.Target) || !world.IsRegistered<WorldTransform>())
            return std::nullopt;
        const WorldTransform* transform = world.TryGet<WorldTransform>(order.Target);
        return transform != nullptr ? std::optional<Vec3d>(transform->Value.Position) : std::nullopt;
    }

    float AgentSeconds(const World& world, EntityId agent) const
    {
        const ActorTime* clock = world.TryGet<ActorTime>(agent);
        return DeltaSeconds * (clock != nullptr ? static_cast<float>(ActorScaleValue(clock->ScaleQ16)) : 1.0f);
    }

    const NavTraversalExecutor* ExecutorFor(const World& world, GameplayTagId kind) const
    {
        const NavTraversalExecutors* table = world.TryGetResource<NavTraversalExecutors>();
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        return table != nullptr && tags != nullptr ? table->Resolve(*tags, kind) : nullptr;
    }

    const NavRouteStep* CrossedStep(EntityId agent, const NavAgentProgress& progress)
    {
        NavRouteSlot* slot = SlotOf(agent, progress);
        if (progress.Phase != NavAgentPhase::Traversing || slot == nullptr || !slot->HasRoute
            || slot->Step >= slot->Route.Steps().size())
            return nullptr;
        const NavRouteStep& step = slot->Route.Steps()[slot->Step];
        return step.Kind == NavRouteStepKind::Traverse ? &step : nullptr;
    }

    void AbortCrossing(World& world, EntityId agent, const NavAgentProgress& progress)
    {
        if (const NavRouteStep* step = CrossedStep(agent, progress))
            if (const NavTraversalExecutor* executor = ExecutorFor(world, step->Traversal);
                executor != nullptr && executor->Abort != nullptr)
                executor->Abort(world, agent, *step);
    }

    // The executor has already stopped (failed, timed out and aborted, or aborted
    // because its link changed), so the crossing is over before anything ends.
    void CrossingFailed(World& world, EntityId agent, NavAgentProgress& progress, NavRouteSlot& slot, NavLinkId link)
    {
        progress.Phase = NavAgentPhase::Planning;
        std::size_t free = 0;
        for (std::size_t i = 0; i < slot.Avoid.size(); ++i)
            if (slot.Avoid[i] == link || slot.AvoidUntilTick[i] <= Tick)
            {
                free = i;
                break;
            }
        slot.Avoid[free] = link;
        slot.AvoidUntilTick[free] = Tick + TicksFor(Settings.AvoidSeconds);
        if (++slot.CrossingFailures >= 2)
        {
            End(world, agent, progress, NavAgentPhase::Failed, NavAgentFailure::TraversalFailed);
            return;
        }
        slot.ReplanningAroundFailedCrossing = true;
        slot.ReplanAfterCrossing = NavRepathReason::None;
        Replan(world, agent, progress, slot, NavRepathReason::TraversalFailed);
    }

    // Whether the link a route step crosses changed since the route was planned.
    bool CrossedLinkChanged(const NavRouteSlot& slot, const NavRouteStep& step) const
    {
        const ZoneNavigation* navigation = FindZoneNavigation(Runtime, slot.Route.Zone);
        const std::optional<std::uint32_t> index = navigation != nullptr ? navigation->FindLink(step.Link) : std::nullopt;
        if (!index)
            return true;
        for (const NavRouteLinkDependency& dependency : slot.Route.LinkDependencies())
            if (dependency.Link == step.Link)
                return navigation->Link(*index).Revision != dependency.Revision;
        return false;
    }

    // -- Lifetime -------------------------------------------------------------

    void Reclaim(World& world)
    {
        if (world.StructuralVersion() == StructuralVersion)
            return;
        StructuralVersion = world.StructuralVersion();
        Slots.Reclaim(
            [&](EntityId owner) { return world.IsAlive(owner) && world.HasComponent<NavAgent>(owner); },
            [&](EntityId owner, std::uint32_t) {
                Queue.Remove(owner);
                if (world.IsAlive(owner))
                    ZeroIntent(world, owner);
            });
    }

    // -- Pass 1: take up new orders and check standing routes ----------------

    void Adopt(World& world, EntityId agent, const NavAgentRequest& order, NavAgentProgress& progress)
    {
        AbortCrossing(world, agent, progress);
        Queue.Remove(agent);
        Slots.Release(progress.RouteSlot);
        progress = NavAgentProgress{};
        progress.Request = order.Id;
        if (order.Kind == NavDestinationKind::None)
        {
            progress.Phase = NavAgentPhase::Cancelled;
            ZeroIntent(world, agent);
            return;
        }
        progress.Phase = NavAgentPhase::Planning;
        progress.RouteSlot = Slots.Acquire(agent);
        Queue.Queue(agent, order.Id, NavPlanClass::Blocking, Tick, Settings.RefreshDeferTicks);
    }

    void Sweep(World& world, EntityId agent)
    {
        const NavAgentRequest* order = std::as_const(world).TryGet<NavAgentRequest>(agent);
        NavAgentProgress* progress = world.TryGet<NavAgentProgress>(agent);
        if (order == nullptr || progress == nullptr)
            return;
        if (order->Id != progress->Request)
            Adopt(world, agent, *order, *progress);
        if (!IsActivePhase(progress->Phase))
            return;
        if (Controlled(world, agent))
        {
            End(world, agent, *progress, NavAgentPhase::Failed, NavAgentFailure::TakenOverByInput, false);
            return;
        }
        NavRouteSlot* slot = SlotOf(agent, *progress);
        if (slot == nullptr)
        {
            // A copied component names a slot this entity does not own.
            progress->RouteSlot = Slots.Acquire(agent);
            progress->Phase = NavAgentPhase::Planning;
            Queue.Queue(agent, progress->Request, NavPlanClass::Blocking, Tick, Settings.RefreshDeferTicks);
            return;
        }
        const std::optional<ZoneId> zone = Runtime.ZoneOf(agent);
        if (!zone)
        {
            End(world, agent, *progress, NavAgentPhase::Failed, NavAgentFailure::NoZone);
            return;
        }
        const std::optional<Vec3d> target = TargetPosition(world, *order);
        if (!target)
        {
            End(world, agent, *progress, NavAgentPhase::Failed, NavAgentFailure::TargetLost);
            return;
        }
        if (order->Kind == NavDestinationKind::Entity)
        {
            const std::optional<ZoneId> targetZone = Runtime.ZoneOf(order->Target);
            if (targetZone && *targetZone != *zone)
            {
                End(world, agent, *progress, NavAgentPhase::Failed, NavAgentFailure::DestinationInOtherZone);
                return;
            }
        }

        if (progress->Phase == NavAgentPhase::Holding)
        {
            const WorldTransform* self = std::as_const(world).TryGet<WorldTransform>(agent);
            const float radius = order->ArriveRadius > 0.0f ? order->ArriveRadius : Settings.ArriveRadius;
            if (self != nullptr
                && NavPlanarDistance(self->Value.Position, *target) > radius * Settings.FollowHysteresis)
                Replan(world, agent, *progress, *slot, NavRepathReason::TargetMoved);
            return;
        }
        if (!slot->HasRoute)
        {
            if (Queue.Find(agent) == nullptr)
                Queue.Queue(agent, progress->Request, NavPlanClass::Blocking, Tick, Settings.RefreshDeferTicks);
            return;
        }

        if (slot->Route.Zone != *zone)
        {
            Replan(world, agent, *progress, *slot, NavRepathReason::ZoneChanged);
            return;
        }
        const NavRouteValidity validity = NavigationQuery(Runtime).ValidateRoute(slot->Route);
        if (validity != NavRouteValidity::Valid)
        {
            const NavRouteStep* crossed = CrossedStep(agent, *progress);
            if (crossed != nullptr && CrossedLinkChanged(*slot, *crossed))
            {
                const NavLinkId link = crossed->Link;
                AbortCrossing(world, agent, *progress);
                CrossingFailed(world, agent, *progress, *slot, link);
            }
            else if (crossed != nullptr)
                slot->ReplanAfterCrossing = RepathReasonForValidity(validity);
            else
                Replan(world, agent, *progress, *slot, RepathReasonForValidity(validity));
            return;
        }
        if (order->Kind == NavDestinationKind::Entity && progress->Phase == NavAgentPhase::Moving
            && Queue.Find(agent) == nullptr)
        {
            const float moved = NavPlanarDistance(*target, slot->PlannedTarget);
            const float threshold =
                std::max(Settings.RetargetDistance, Settings.RetargetFraction * progress->Remaining);
            if (moved > threshold && Tick >= slot->PlanTick + TicksFor(Settings.RepathInterval))
            {
                progress->LastRepath = NavRepathReason::TargetMoved;
                ++progress->Repaths;
                ++LastStats.Repaths;
                Queue.Queue(agent, progress->Request, NavPlanClass::Refresh, Tick, Settings.RefreshDeferTicks);
            }
        }
    }

    // -- Pass 2: one planning batch ------------------------------------------

    bool Gather(World& world, EntityId agent, NavPlanInput& input)
    {
        const NavAgent* config = std::as_const(world).TryGet<NavAgent>(agent);
        const NavAgentRequest* order = std::as_const(world).TryGet<NavAgentRequest>(agent);
        NavAgentProgress* progress = world.TryGet<NavAgentProgress>(agent);
        const WorldTransform* self = std::as_const(world).TryGet<WorldTransform>(agent);
        NavRouteSlot* slot = progress != nullptr ? SlotOf(agent, *progress) : nullptr;
        if (config == nullptr || order == nullptr || slot == nullptr || self == nullptr)
            return false;
        const auto fail = [&](NavAgentFailure failure) {
            End(world, agent, *progress, NavAgentPhase::Failed, failure);
            return false;
        };
        const std::optional<ZoneId> zone = Runtime.ZoneOf(agent);
        const ZoneNavigation* navigation = zone ? FindZoneNavigation(Runtime, *zone) : nullptr;
        if (navigation == nullptr)
            return fail(zone ? NavAgentFailure::ZoneUnavailable : NavAgentFailure::NoZone);
        const GameplayTagRegistry* tags = std::as_const(world).TryGetResource<GameplayTagRegistry>();
        const GameplayTagId profile = tags != nullptr ? tags->FindTag(config->Profile.View()) : GameplayTagId{};
        const std::optional<std::uint16_t> profileIndex =
            profile.IsValid() ? navigation->FindProfile(profile) : std::nullopt;
        if (!profileIndex)
            return fail(NavAgentFailure::ProfileUnavailable);
        const std::optional<Vec3d> target = TargetPosition(world, *order);
        if (!target)
            return fail(NavAgentFailure::TargetLost);

        const NavBuildProfile& build = navigation->Mesh(*profileIndex).Profile();
        slot->Height = build.Height;
        if (world.IsRegistered<CharacterController>())
            if (const CharacterController* capsule = std::as_const(world).TryGet<CharacterController>(agent);
                capsule != nullptr && capsule->Radius > build.Radius)
                ++LastStats.AgentsWiderThanProfile;

        input = NavPlanInput{};
        input.Zone = navigation;
        input.Profile = profile;
        input.Policy = Policies.Bind(config->Policy, *tags);
        input.From = self->Value.Position;
        input.To = *target;
        const float horizontal = std::max(build.Radius * 2.0f, 0.5f);
        input.Extents = Vec3d(horizontal, build.Height, horizontal);
        for (std::size_t i = 0; i < slot->Avoid.size(); ++i)
            if (slot->Avoid[i].IsValid() && slot->AvoidUntilTick[i] > Tick)
                input.AvoidLinks[input.AvoidLinkCount++] = slot->Avoid[i];
        GatherCapabilities(world, agent, *navigation, *tags, input);
        return true;
    }

    // The zone's link kinds this agent may cross: its tags grant the kind or an
    // ancestor of it, and an executor is registered to carry it out. A crossing
    // nobody can perform is never planned.
    void GatherCapabilities(const World& world, EntityId agent, const ZoneNavigation& navigation,
                            const GameplayTagRegistry& tags, NavPlanInput& input) const
    {
        const NavTraversalExecutors* table = world.TryGetResource<NavTraversalExecutors>();
        const GameplayTagContainer* held =
            world.IsRegistered<GameplayTagContainer>() ? world.TryGet<GameplayTagContainer>(agent) : nullptr;
        if (table == nullptr || held == nullptr)
            return;
        const auto granted = [&](GameplayTagId kind) {
            for (std::uint8_t i = 0; i < held->Count; ++i)
                if (tags.IsDescendantOf(kind, held->Tags[i]))
                    return true;
            return false;
        };
        for (std::uint32_t i = 0; i < navigation.LinkCount(); ++i)
        {
            const GameplayTagId kind = navigation.Link(i).Traversal;
            const auto chosen = std::span(input.Capabilities.data(), input.CapabilityCount);
            // The zone binds at most kNavMaxTraversalKinds kinds; past that a kind is unusable anyway.
            if (!kind.IsValid() || input.CapabilityCount == input.Capabilities.size()
                || std::ranges::find(chosen, kind) != chosen.end())
                continue;
            if (granted(kind) && table->Resolve(tags, kind) != nullptr)
                input.Capabilities[input.CapabilityCount++] = kind;
        }
    }

    void Commit(World& world, EntityId agent, const NavPlanInput& input, std::size_t index)
    {
        NavAgentProgress* progress = world.TryGet<NavAgentProgress>(agent);
        NavRouteSlot* slot = progress != nullptr ? SlotOf(agent, *progress) : nullptr;
        if (slot == nullptr)
            return;
        const NavPlanOutput& output = Planner.Output(index);
        progress->LastQuery = output.Status;
        slot->LastDiagnostics = output.Diagnostics;
        if (output.Status == NavStatus::Success)
        {
            std::swap(slot->Route, Planner.Route(index));
            slot->HasRoute = true;
            slot->Step = 0;
            slot->Corner = 0;
            slot->LegStart = output.Start.Position;
            slot->Stuck = NavStuckWindow{};
            slot->OffMesh = false;
            slot->ReplanningAroundFailedCrossing = false;
            slot->PlannedTarget = input.To;
            slot->PlanTick = Tick;
            progress->Phase = NavAgentPhase::Moving;
            progress->Destination = input.To;
            return;
        }
        if (output.Status == NavStatus::InvalidStart)
        {
            if (!slot->OffMesh)
            {
                slot->OffMesh = true;
                slot->OffMeshSinceTick = Tick;
            }
            if (Tick < slot->OffMeshSinceTick + TicksFor(Settings.OffMeshGrace))
            {
                slot->HasRoute = false;
                progress->Phase = NavAgentPhase::Planning;
                Queue.Queue(agent, progress->Request, NavPlanClass::Blocking, Tick + 1, Settings.RefreshDeferTicks);
                return;
            }
        }
        End(world, agent, *progress, NavAgentPhase::Failed,
            FailureForSearchStatus(output.Status, slot->ReplanningAroundFailedCrossing));
    }

    void Plan(World& world)
    {
        const auto planning = [&](const NavPlanEntry& entry) {
            if (!world.IsAlive(entry.Agent) || !Partitions->Contains(world.GetEntityPartition(entry.Agent)))
                return false;
            const NavAgentProgress* progress = std::as_const(world).TryGet<NavAgentProgress>(entry.Agent);
            return progress != nullptr && progress->Request == entry.Request
                && progress->Phase != NavAgentPhase::Traversing;
        };
        Admitted.clear();
        Queue.Take(Settings.PlansPerTick, planning, Admitted);
        Inputs.clear();
        InputAgents.clear();
        for (const NavPlanEntry& entry : Admitted)
        {
            NavPlanInput input;
            if (!Gather(world, entry.Agent, input))
                continue;
            Inputs.push_back(input);
            InputAgents.push_back(entry.Agent);
        }
        LastStats.Planned = Inputs.size();
        LastStats.PlannedInParallel = Jobs != nullptr && Jobs->WorkerCount() > 0
            && Inputs.size() >= std::max<std::size_t>(Settings.ParallelMinBatch, 2);
        Planner.Plan(Inputs, Settings.ParallelMinBatch);
        for (std::size_t i = 0; i < Inputs.size(); ++i)
            Commit(world, InputAgents[i], Inputs[i], i);
    }

    // -- Pass 3: follow --------------------------------------------------------

    void Steer(World& world, EntityId agent, const Vec3d& position, const Vec3d& toward, float magnitude)
    {
        if (MovementIntent* intent = world.TryGet<MovementIntent>(agent))
        {
            intent->WishDir = NavPlanarDirection(position, toward) * magnitude;
            intent->Jump = false;
        }
    }

    // A point is reached at the route's end, which is the point projected onto the
    // mesh. A target is reached by standing near it, or at the route's end when
    // the target stands just off the mesh beside it.
    bool HasArrived(const World& world, const NavAgentRequest& order, const Vec3d& position, const Vec3d& end,
                    float radius, float height) const
    {
        const bool atEnd = NavHasArrived(position, end, radius, height);
        if (order.Kind != NavDestinationKind::Entity)
            return atEnd;
        const std::optional<Vec3d> target = TargetPosition(world, order);
        return target
            && (NavHasArrived(position, *target, radius, height)
                || (atEnd && NavPlanarDistance(end, *target) <= radius));
    }

    void Arrive(World& world, EntityId agent, const NavAgentRequest& order, NavAgentProgress& progress,
                NavRouteSlot& slot)
    {
        progress.Remaining = 0.0f;
        if (order.Kind == NavDestinationKind::Entity && order.Follow)
        {
            progress.Phase = NavAgentPhase::Holding;
            slot.HasRoute = false;
            ZeroIntent(world, agent);
            return;
        }
        End(world, agent, progress, NavAgentPhase::Arrived, NavAgentFailure::None);
    }

    void Follow(World& world, EntityId agent)
    {
        const NavAgentRequest* order = std::as_const(world).TryGet<NavAgentRequest>(agent);
        NavAgentProgress* progress = world.TryGet<NavAgentProgress>(agent);
        if (order == nullptr || progress == nullptr || !IsActivePhase(progress->Phase))
            return;
        NavRouteSlot* slot = SlotOf(agent, *progress);
        const WorldTransform* self = std::as_const(world).TryGet<WorldTransform>(agent);
        if (slot == nullptr || self == nullptr || !slot->HasRoute || progress->Phase == NavAgentPhase::Planning
            || progress->Phase == NavAgentPhase::Holding)
        {
            ZeroIntent(world, agent);
            return;
        }
        const Vec3d position = self->Value.Position;
        const NavFollowTolerances tolerances{ Settings.CornerRadius, Settings.Deviation, slot->Height };
        const float arriveRadius = order->ArriveRadius > 0.0f ? order->ArriveRadius : Settings.ArriveRadius;
        const auto steps = slot->Route.Steps();
        const auto corners = slot->Route.Corners();

        while (slot->Step < steps.size() && steps[slot->Step].Kind == NavRouteStepKind::Walk
               && steps[slot->Step].CornerCount == 0)
            ++slot->Step;
        if (slot->Step >= steps.size())
        {
            if (HasArrived(world, *order, position, slot->Route.Corners().empty() ? position
                                                                                  : slot->Route.Corners().back(),
                           arriveRadius, slot->Height))
                Arrive(world, agent, *order, *progress, *slot);
            else
                Replan(world, agent, *progress, *slot,
                       order->Kind == NavDestinationKind::Entity ? NavRepathReason::TargetMoved
                                                                 : NavRepathReason::Deviated);
            return;
        }
        const NavRouteStep& step = steps[slot->Step];
        if (step.Kind == NavRouteStepKind::Traverse)
        {
            Cross(world, agent, *progress, *slot, step, position);
            return;
        }

        const auto leg = corners.subspan(step.FirstCorner, step.CornerCount);
        const NavWalkAdvance advance = NavAdvanceAlongWalk(leg, slot->LegStart, slot->Corner, position, tolerances);
        if (advance.Deviated)
        {
            Replan(world, agent, *progress, *slot, NavRepathReason::Deviated);
            return;
        }
        if (advance.Corner > slot->Corner)
            slot->StuckReplans = 0;
        slot->Corner = advance.Corner;

        const bool lastCorner = slot->Corner + 1 == leg.size();
        const bool finalLeg = slot->Step + 1 == steps.size();
        if (lastCorner && !finalLeg && steps[slot->Step + 1].Kind == NavRouteStepKind::Traverse)
        {
            const NavRouteStep& link = steps[slot->Step + 1];
            if (NavHasArrived(position, link.Entry, std::max(link.EntryRadius, Settings.CornerRadius), slot->Height))
            {
                ++slot->Step;
                slot->Corner = 0;
                slot->CrossingTicks = 0;
                slot->CrossingSeconds = 0.0f;
                slot->CrossingMemory = 0;
                Cross(world, agent, *progress, *slot, link, position);
                return;
            }
        }
        float magnitude = order->SpeedScale;
        if (lastCorner && finalLeg)
        {
            const Vec3d end = leg.back();
            if (HasArrived(world, *order, position, end, arriveRadius, slot->Height))
            {
                Arrive(world, agent, *order, *progress, *slot);
                return;
            }
            const KinematicState* motion = std::as_const(world).TryGet<KinematicState>(agent);
            const ResolvedMovementTuning* tuning = std::as_const(world).TryGet<ResolvedMovementTuning>(agent);
            const float speed = motion != nullptr
                ? std::sqrt(motion->Velocity.X * motion->Velocity.X + motion->Velocity.Z * motion->Velocity.Z)
                : 0.0f;
            magnitude *= NavArrivalWishScale(NavPlanarDistance(position, end), speed,
                                             tuning != nullptr ? tuning->Friction : 0.0f, arriveRadius);
        }
        Steer(world, agent, position, leg[slot->Corner], magnitude);

        progress->Remaining = RouteRemaining(slot->Route, slot->Step, slot->Corner, position);
        if (ObserveStuck(world, agent, *progress, *slot, magnitude))
            return;
    }

    void Cross(World& world, EntityId agent, NavAgentProgress& progress, NavRouteSlot& slot, const NavRouteStep& step,
               const Vec3d& position)
    {
        const NavTraversalExecutor* executor = ExecutorFor(world, step.Traversal);
        MovementIntent* intent = world.TryGet<MovementIntent>(agent);
        if (executor == nullptr || intent == nullptr)
        {
            End(world, agent, progress, NavAgentPhase::Failed, NavAgentFailure::TraversalUnsupported);
            return;
        }
        progress.Phase = NavAgentPhase::Traversing;
        const float seconds = AgentSeconds(world, agent);
        NavTraversalFrame frame{ world, agent, step, slot.CrossingTicks, seconds, slot.Height, *intent,
                                 slot.CrossingMemory };
        NavTraversalResult result = executor->Step(frame);
        ++slot.CrossingTicks;
        slot.CrossingSeconds += seconds;
        progress.Remaining = RouteRemaining(slot.Route, slot.Step, 0, position);
        if (result == NavTraversalResult::Running && slot.CrossingSeconds > Settings.TraversalTimeout)
        {
            AbortCrossing(world, agent, progress);
            result = NavTraversalResult::Failed;
        }
        if (result == NavTraversalResult::Failed)
        {
            CrossingFailed(world, agent, progress, slot, step.Link);
            return;
        }
        if (result != NavTraversalResult::Succeeded)
            return;
        ++slot.Step;
        slot.Corner = 0;
        slot.LegStart = step.Exit;
        slot.Stuck = NavStuckWindow{};
        progress.Phase = NavAgentPhase::Moving;
        if (slot.ReplanAfterCrossing != NavRepathReason::None)
        {
            const NavRepathReason reason = slot.ReplanAfterCrossing;
            slot.ReplanAfterCrossing = NavRepathReason::None;
            Replan(world, agent, progress, slot, reason);
        }
    }

    // -- Debug drawing ---------------------------------------------------------

    void DrawAgent(const World& world, DebugLineList& lines, EntityId agent)
    {
        const NavAgentProgress* progress = world.TryGet<NavAgentProgress>(agent);
        const WorldTransform* self = world.TryGet<WorldTransform>(agent);
        if (progress == nullptr || self == nullptr || !IsActivePhase(progress->Phase))
            return;
        const bool selected = agent == Selected;
        const Vec3d lift(0.0f, kLift, 0.0f);
        Vec3d from = self->Value.Position;
        const auto add = [&](const Vec3d& a, const Vec3d& b, const Vec4& color) {
            if (selected)
                lines.AddOnTop(a + lift, b + lift, kSelectedColor);
            else
                lines.AddTested(a + lift, b + lift, color);
        };
        AppendMarker(lines, progress->Destination, kMarkerSize, selected ? kSelectedColor : kRouteColor);
        const NavRouteSlot* slot = SlotOf(agent, *progress);
        if (slot == nullptr || !slot->HasRoute)
        {
            add(from, progress->Destination, kWaitingColor);
            return;
        }
        const auto steps = slot->Route.Steps();
        const auto corners = slot->Route.Corners();
        for (std::uint32_t s = slot->Step; s < steps.size(); ++s)
        {
            const NavRouteStep& step = steps[s];
            if (step.Kind == NavRouteStepKind::Traverse)
            {
                add(from, step.Entry, kRouteColor);
                add(step.Entry, step.Exit, kCrossingColor);
                from = step.Exit;
                continue;
            }
            const auto leg = corners.subspan(step.FirstCorner, step.CornerCount);
            for (std::uint32_t c = s == slot->Step ? slot->Corner : 0; c < leg.size(); ++c)
            {
                add(from, leg[c], kRouteColor);
                from = leg[c];
            }
        }
        if (selected)
            if (const MovementIntent* intent = world.TryGet<MovementIntent>(agent))
                lines.AddOnTop(self->Value.Position, self->Value.Position + intent->WishDir, kCrossingColor);
    }

    void Draw(World& world)
    {
        const bool draw = ReadCVarBool(Console, kDebugDrawCVar, false);
        if (!draw && !Drawing)
            return;
        if (!world.HasResource<DebugLineChannels>())
            world.AddResource<DebugLineChannels>();
        DebugLineChannels& channels = world.GetResource<DebugLineChannels>();
        DebugLineList& lines = channels.Lines(channels.Channel(kDebugChannel));
        lines.Clear();
        if (draw)
            for (const EntityId agent : Active)
                DrawAgent(world, lines, agent);
        Drawing = draw;
    }

    // True when the agent was judged stuck and its route dropped or ended.
    bool ObserveStuck(World& world, EntityId agent, NavAgentProgress& progress, NavRouteSlot& slot, float magnitude)
    {
        const MotionRequest* request = std::as_const(world).TryGet<MotionRequest>(agent);
        const ResolvedMovementTuning* tuning = std::as_const(world).TryGet<ResolvedMovementTuning>(agent);
        const float agentSeconds = AgentSeconds(world, agent);
        const float expected = magnitude * (tuning != nullptr ? tuning->MaxSpeed : 0.0f) * agentSeconds;
        const bool counts = request == nullptr || !request->PlanarOverridden;
        if (!NavObserveStuck(slot.Stuck, progress.Remaining, expected, agentSeconds, counts, Settings.Stuck))
            return false;
        if (++slot.StuckReplans > Settings.StuckRepaths)
        {
            End(world, agent, progress, NavAgentPhase::Failed, NavAgentFailure::Stuck);
            return true;
        }
        Replan(world, agent, progress, slot, NavRepathReason::Stuck);
        return true;
    }
};

NavAgentSystem::NavAgentSystem(Services services)
    : Impl(std::make_unique<State>(services))
{
    if (services.Console != nullptr)
        RegisterCVars(*services.Console);
}

NavAgentSystem::~NavAgentSystem() = default;

void NavAgentSystem::FixedLogic(FixedLogicContext& ctx)
{
    State& state = *Impl;
    World& world = ctx.Entities;
    state.LastStats = Stats{};
    if (!world.IsRegistered<NavAgent>() || !IsSimulationAuthority(world))
        return;
    state.Settings = ReadSettings(state.Console);
    state.Tick = ctx.Time.TickIndex;
    state.DeltaSeconds = static_cast<float>(ctx.Time.DeltaSeconds);
    state.Partitions = &ctx.Partitions;
    state.Reclaim(world);

    if (!state.Agents)
        state.Agents.emplace(world);
    state.Active.clear();
    state.Agents->ForEachChunkIn(ctx.Partitions, [&](auto& view) {
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            state.Active.push_back(view.Entity(i));
    });
    std::sort(state.Active.begin(), state.Active.end());
    state.LastStats.Agents = state.Active.size();

    for (const EntityId agent : state.Active)
        state.Sweep(world, agent);
    state.Plan(world);
    for (const EntityId agent : state.Active)
        state.Follow(world, agent);

    for (const EntityId agent : state.Active)
        if (const NavAgentProgress* progress = std::as_const(world).TryGet<NavAgentProgress>(agent);
            progress != nullptr && IsActivePhase(progress->Phase))
            ++state.LastStats.Active;
    state.LastStats.Queued = state.Queue.Size();
    state.Draw(world);
}

const NavAgentSystem::Stats& NavAgentSystem::LastStats() const { return Impl->LastStats; }
std::span<const EntityId> NavAgentSystem::LastAgents() const { return Impl->Active; }
void NavAgentSystem::SelectAgent(EntityId agent) { Impl->Selected = agent; }
EntityId NavAgentSystem::SelectedAgent() const { return Impl->Selected; }

bool NavAgentSystem::Inspect(EntityId agent, NavAgentInspection& out) const
{
    State& state = *Impl;
    const World& world = state.Runtime.Entities();
    const NavAgentProgress* progress =
        world.IsRegistered<NavAgentProgress>() ? world.TryGet<NavAgentProgress>(agent) : nullptr;
    const NavRouteSlot* slot = progress != nullptr ? state.SlotOf(agent, *progress) : nullptr;
    if (slot == nullptr)
        return false;
    out = NavAgentInspection{};
    out.HasRoute = slot->HasRoute;
    out.Queued = state.Queue.Find(agent) != nullptr;
    out.StepCount = static_cast<std::uint32_t>(slot->Route.Steps().size());
    out.Step = slot->Step;
    out.Corner = slot->Corner;
    if (slot->HasRoute && slot->Step < out.StepCount)
    {
        const NavRouteStep& step = slot->Route.Steps()[slot->Step];
        out.Crossing = step.Kind == NavRouteStepKind::Traverse;
        out.LegCorners = step.CornerCount;
        out.Link = step.Link;
    }
    out.CrossingTicks = slot->CrossingTicks;
    out.CrossingFailures = slot->CrossingFailures;
    out.StuckReplans = slot->StuckReplans;
    out.StuckElapsed = slot->Stuck.Elapsed;
    out.StuckExpected = slot->Stuck.Expected;
    out.LastSearch = slot->LastDiagnostics;
    return true;
}
const NavPlanQueue& NavAgentSystem::Queue() const { return Impl->Queue; }
std::size_t NavAgentSystem::RouteSlotsInUse() const { return Impl->Slots.InUseCount(); }
std::size_t NavAgentSystem::PlanningContexts() const { return Impl->Planner.ContextCount(); }
