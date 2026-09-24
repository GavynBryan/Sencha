#include <navigation/NavigationSystem.h>

#include "NavGeometryTracker.h"
#include "NavLinkStateSync.h"
#include "NavTileRebuilder.h"

#include <app/GameContexts.h>
#include <core/console/CVarRead.h>
#include <core/console/ConsoleRegistry.h>
#include <navigation/ZoneNavigation.h>
#include <world/RuntimeWorld.h>

#include <algorithm>

namespace
{
    constexpr std::string_view kTilesPerTickCVar = "nav.rebuild.tiles_per_tick";
    constexpr std::string_view kMoveThresholdCVar = "nav.rebuild.move_threshold";
    constexpr double kDefaultTilesPerTick = 4.0;
    constexpr double kDefaultMoveThreshold = 0.05;

    void RegisterCVars(ConsoleRegistry& console)
    {
        console.RegisterCVar({
            .Name = std::string(kTilesPerTickCVar),
            .Owner = "navigation",
            .Type = CVarType::Double,
            .DefaultValue = kDefaultTilesPerTick,
            .CurrentValue = kDefaultTilesPerTick,
            .Help = "Navigation tiles rebuilt per fixed tick after runtime geometry changes.",
            .Source = { "navigation" },
            .Min = 1.0,
            .Max = 256.0,
        });
        console.RegisterCVar({
            .Name = std::string(kMoveThresholdCVar),
            .Owner = "navigation",
            .Type = CVarType::Double,
            .DefaultValue = kDefaultMoveThreshold,
            .CurrentValue = kDefaultMoveThreshold,
            .Help = "Metres a navigation-geometry collider must move before its tiles rebuild.",
            .Source = { "navigation" },
            .Min = 0.0,
            .Max = 10.0,
        });
    }
}

struct NavigationSystem::State
{
    State(RuntimeWorld& world, JobSystem* jobs, ConsoleRegistry* console)
        : World(world), Jobs(jobs), Console(console) {}

    RuntimeWorld& World;
    JobSystem* Jobs = nullptr;
    ConsoleRegistry* Console = nullptr;
    std::vector<ZoneId> Zones;
    // Attached since the last geometry scan, so cooked without today's
    // contributors; the next scan applies every contributor to them.
    std::vector<ZoneId> ZonesAwaitingContributors;
    NavGeometryTracker Geometry;
    NavTileRebuilder Rebuilder;
    NavLinkStateSync Links;
    LinkStateStats LinkStats;
    RebuildStats Rebuild;

    void MarkEverywhere(const Aabb3d& bounds)
    {
        for (const ZoneId zone : Zones)
            if (const ZoneNavigation* navigation = FindZoneNavigation(World, zone))
                Rebuilder.MarkDirty(*navigation, bounds);
    }

    void ApplyContributorsToNewZones()
    {
        for (const ZoneId zone : ZonesAwaitingContributors)
            if (const ZoneNavigation* navigation = FindZoneNavigation(World, zone))
                for (const NavGeometryContributor& contributor : Geometry.Contributors())
                    Rebuilder.MarkDirty(*navigation, contributor.Bounds);
        ZonesAwaitingContributors.clear();
    }
};

NavigationSystem::NavigationSystem(RuntimeWorld& world, JobSystem* jobs, ConsoleRegistry* console)
    : Impl(std::make_unique<State>(world, jobs, console))
{
    if (console != nullptr)
        RegisterCVars(*console);
}

NavigationSystem::~NavigationSystem() = default;

void NavigationSystem::ZoneResidency(ZoneResidencyContext& ctx)
{
    for (const ZoneResidencyChange& change : ctx.Changes)
    {
        if (change.Kind == ZoneResidencyChangeKind::Attached)
        {
            const ZoneNavigation* navigation = FindZoneNavigation(Impl->World, change.Zone);
            if (navigation == nullptr || std::ranges::find(Impl->Zones, change.Zone) != Impl->Zones.end())
                continue;
            Impl->Zones.push_back(change.Zone);
            Impl->ZonesAwaitingContributors.push_back(change.Zone);
            Impl->Links.IndexZone(change.Zone, *navigation);
        }
        else if (change.Kind == ZoneResidencyChangeKind::Detaching)
        {
            std::erase(Impl->Zones, change.Zone);
            std::erase(Impl->ZonesAwaitingContributors, change.Zone);
            Impl->Links.ForgetZone(change.Zone);
            Impl->Rebuilder.ForgetZone(change.Zone);
        }
    }
}

void NavigationSystem::PostFixed(PostFixedContext&)
{
    DetectGeometryChanges();
    RebuildDirtyTiles();
    ApplyLinkState();
}

void NavigationSystem::DetectGeometryChanges()
{
    State& state = *Impl;
    state.Rebuild = RebuildStats{};
    if (state.Zones.empty())
        return;
    std::vector<Aabb3d> changed;
    const float threshold = static_cast<float>(
        ReadCVarDouble(state.Console, kMoveThresholdCVar, kDefaultMoveThreshold));
    const NavGeometryTracker::ScanResult scan =
        state.Geometry.Scan(state.World.Entities(), threshold, changed);
    state.Rebuild.Contributors = scan.Contributors;
    state.Rebuild.SkippedMeshContributors = scan.SkippedMeshColliders;
    for (const Aabb3d& bounds : changed)
        state.MarkEverywhere(bounds);
    state.ApplyContributorsToNewZones();
    state.Rebuild.DirtyTiles = state.Rebuilder.Pending();
}

void NavigationSystem::RebuildDirtyTiles()
{
    State& state = *Impl;
    const std::size_t budget = static_cast<std::size_t>(
        std::max(1.0, ReadCVarDouble(state.Console, kTilesPerTickCVar, kDefaultTilesPerTick)));
    const NavTileRebuilder::BatchResult batch = state.Rebuilder.RebuildNext(
        state.World, state.Geometry.Contributors(), state.Jobs, budget);
    state.Rebuild.RebuiltTiles = batch.Rebuilt;
    state.Rebuild.LinksReprojected = batch.LinksReattached;
}

void NavigationSystem::ApplyLinkState()
{
    const NavLinkStateSync::ApplyResult applied = Impl->Links.Apply(Impl->World);
    Impl->LinkStats = LinkStateStats{ applied.StateComponents, applied.UnknownLinks,
                                      applied.LinksChanged };
}

NavigationQuery NavigationSystem::Queries() const { return NavigationQuery(Impl->World); }
std::span<const ZoneId> NavigationSystem::Zones() const { return Impl->Zones; }

std::optional<ZoneId> NavigationSystem::FindLinkZone(NavLinkId link) const
{
    return Impl->Links.FindLinkZone(link);
}

const NavigationSystem::LinkStateStats& NavigationSystem::LastLinkStateStats() const
{
    return Impl->LinkStats;
}

const NavigationSystem::RebuildStats& NavigationSystem::LastRebuildStats() const
{
    return Impl->Rebuild;
}

std::size_t NavigationSystem::PendingDirtyTiles() const { return Impl->Rebuilder.Pending(); }
