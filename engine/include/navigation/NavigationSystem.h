#pragma once

#include <navigation/NavigationIds.h>
#include <navigation/NavigationQuery.h>
#include <zone/ZoneId.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>

class ConsoleRegistry;
class JobSystem;
class RuntimeWorld;
struct PostFixedContext;
struct ZoneResidencyContext;

// Navigation's schedule-bound work, registered by the engine for every host.
// Once per fixed tick, after all fixed logic, it rebuilds the tiles runtime
// geometry changed and applies gameplay link state, so what gameplay does in
// tick T is what every query in tick T+1 sees. Tunables:
// `nav.rebuild.tiles_per_tick`, `nav.rebuild.move_threshold`. See
// docs/navigation/runtime.md.
class NavigationSystem
{
public:
    NavigationSystem(RuntimeWorld& world, JobSystem* jobs, ConsoleRegistry* console = nullptr);
    ~NavigationSystem();

    void ZoneResidency(ZoneResidencyContext& ctx);
    void PostFixed(PostFixedContext& ctx);

    [[nodiscard]] NavigationQuery Queries() const;

    // Resident zones that carry navigation, in attach order.
    [[nodiscard]] std::span<const ZoneId> Zones() const;
    [[nodiscard]] std::optional<ZoneId> FindLinkZone(NavLinkId link) const;

    struct LinkStateStats
    {
        std::size_t StateComponents = 0;
        std::size_t UnknownLinks = 0;
        std::size_t LinksChanged = 0;
    };
    struct RebuildStats
    {
        std::size_t Contributors = 0;
        std::size_t SkippedMeshContributors = 0;
        std::size_t DirtyTiles = 0;
        std::size_t RebuiltTiles = 0;
        std::size_t LinksReprojected = 0;
    };
    [[nodiscard]] const LinkStateStats& LastLinkStateStats() const;
    [[nodiscard]] const RebuildStats& LastRebuildStats() const;

    // PostFixed's steps, in order. Public so tests and tools can drive them.
    void DetectGeometryChanges();
    void RebuildDirtyTiles();
    void ApplyLinkState();
    [[nodiscard]] std::size_t PendingDirtyTiles() const;

private:
    struct State;
    std::unique_ptr<State> Impl;
};
