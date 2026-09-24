#pragma once

#include <gameplay_tags/GameplayTagId.h>
#include <math/Vec.h>
#include <navigation/NavTileBuild.h>
#include <navigation/NavigationIds.h>
#include <zone/ZoneId.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// Navigation query vocabulary. See docs/navigation/runtime.md.
//
// Locations and regions are runtime references into one zone's navigation.
// They carry the zone and the navigation generation they were made against,
// so a reference into an unloaded or reloaded zone -- or into a tile rebuilt
// since -- fails with StaleLocation instead of resolving to something else.
// They are never serialized.
//=============================================================================

enum class NavStatus : std::uint8_t
{
    Success,
    // The search stopped before the destination; the result is the best
    // partial answer (see the operation).
    Partial,
    NoPath,
    InvalidStart,
    InvalidDestination,
    // The zone is not resident, or has no navigation.
    ZoneUnavailable,
    // The zone was not cooked for the requested build profile.
    ProfileUnavailable,
    // A location, region, or route refers to navigation that has since been
    // unloaded, reloaded, or rebuilt.
    StaleLocation,
    // The endpoints belong to different zones. Cross-zone planning is a
    // separate layer above navigation.
    CrossZoneUnsupported,
    // The query context's node budget ran out before the search finished.
    SearchLimitReached,
    // The caller's output buffer is full.
    OutputCapacityReached,
};

[[nodiscard]] const char* NavStatusName(NavStatus status);

// A point on one zone's walkable surface for one build profile.
struct NavLocation
{
    ZoneId Zone;
    std::uint32_t Generation = 0;
    std::uint16_t Profile = 0;
    std::uint16_t Reserved = 0;
    // Opaque polygon reference; zero means "no location".
    std::uint64_t Ref = 0;
    Vec3d Position = Vec3d::Zero();

    [[nodiscard]] bool IsValid() const { return Ref != 0; }
};

// One walkable region (a navmesh polygon) of one zone and profile. A region is
// a place, not a point: ClosestPointInRegion turns it into a location.
struct NavRegion
{
    ZoneId Zone;
    std::uint32_t Generation = 0;
    std::uint16_t Profile = 0;
    std::uint16_t Reserved = 0;
    std::uint64_t Ref = 0;

    [[nodiscard]] bool IsValid() const { return Ref != 0; }
};

// Per-kind adjustment to the cost of crossing navigation links.
struct NavTraversalCost
{
    GameplayTagId Kind;
    float Multiplier = 1.0f;
    float Add = 0.0f;
};

struct NavAreaCost
{
    GameplayTagId Area;
    float Cost = 1.0f;
};

// How an agent weighs the ground and the traversals it can use. Pure data:
// changing a policy changes routes without rebuilding any navigation. Costs are
// relative travel costs (default ground costs 1 per metre), not seconds.
struct NavQueryPolicy
{
    // Areas not listed cost 1. A cost must be positive and finite.
    std::vector<NavAreaCost> AreaCosts;
    // Areas the agent never enters.
    std::vector<GameplayTagId> ForbiddenAreas;
    // Link kinds not listed cross at their base cost.
    std::vector<NavTraversalCost> TraversalCosts;
};

// A caller-owned override of one link's crossing cost for one query, for
// example "this jump failed for me recently; treat it as very expensive".
struct NavLinkCostOverride
{
    NavLinkId Link;
    float Cost = 0.0f;
};

// Everything that shapes one query besides its endpoints. The spans are
// caller-owned transient constraints: navigation reads them during the query
// and keeps nothing, so an agent's memory of failed links stays with the agent.
struct NavQueryRequest
{
    ZoneId Zone;
    GameplayTagId Profile;
    // Null means default costs and nothing forbidden.
    const NavQueryPolicy* Policy = nullptr;
    // Traversal kinds the agent can perform. A link is usable when its kind is
    // one of these or a descendant of one. Empty: no links are usable.
    std::span<const GameplayTagId> Capabilities;
    // Links this query must not use, whatever their global state.
    std::span<const NavLinkId> AvoidLinks;
    std::span<const NavLinkCostOverride> LinkCostOverrides;
};

enum class NavRouteStepKind : std::uint8_t
{
    // Follow Corners[FirstCorner, FirstCorner + CornerCount) across walkable
    // surface. The last corner is where the step ends.
    Walk,
    // Cross a navigation link from Entry to Exit. The traversal kind says what
    // gameplay must do; navigation does not do it.
    Traverse,
};

struct NavRouteStep
{
    NavRouteStepKind Kind = NavRouteStepKind::Walk;
    std::uint32_t FirstCorner = 0;
    std::uint32_t CornerCount = 0;
    NavLinkId Link;
    GameplayTagId Traversal;
    Vec3d Entry = Vec3d::Zero();
    Vec3d Exit = Vec3d::Zero();
    // How close to Entry an agent must be to begin the traversal.
    float EntryRadius = 0.0f;
    // True when the link is crossed exit-to-entry of its authoring (a
    // bidirectional link used in reverse). Entry and Exit are already swapped
    // to the direction of travel.
    bool Reversed = false;
    float Cost = 0.0f;
};

// One tile a route passes over, with its revision when the route was made.
struct NavRouteTileDependency
{
    NavTileCoord Tile;
    std::uint64_t Revision = 0;
};

struct NavRouteLinkDependency
{
    NavLinkId Link;
    std::uint32_t Revision = 0;
};

// Caller-owned route storage with fixed capacity. A query never grows it: a
// route that does not fit fails with OutputCapacityReached. Reuse one buffer
// per agent or per worker.
class NavRouteBuffer
{
public:
    explicit NavRouteBuffer(std::size_t maxSteps = 32, std::size_t maxCorners = 128,
                            std::size_t maxDependencies = 64);

    [[nodiscard]] std::span<const NavRouteStep> Steps() const { return { Steps_.data(), StepCount_ }; }
    [[nodiscard]] std::span<const Vec3d> Corners() const { return { Corners_.data(), CornerCount_ }; }
    [[nodiscard]] std::span<const NavRouteTileDependency> TileDependencies() const
    {
        return { Tiles_.data(), TileCount_ };
    }
    [[nodiscard]] std::span<const NavRouteLinkDependency> LinkDependencies() const
    {
        return { Links_.data(), LinkCount_ };
    }

    ZoneId Zone;
    std::uint32_t Generation = 0;
    std::uint16_t Profile = 0;
    float Cost = 0.0f;
    float WalkDistance = 0.0f;

    void Clear();
    [[nodiscard]] NavRouteStep* PushStep();
    [[nodiscard]] bool PushCorner(const Vec3d& corner);
    [[nodiscard]] bool AddTile(NavTileCoord tile, std::uint64_t revision);
    [[nodiscard]] bool AddLink(NavLinkId link, std::uint32_t revision);
    [[nodiscard]] std::size_t CornerCount() const { return CornerCount_; }

private:
    std::vector<NavRouteStep> Steps_;
    std::vector<Vec3d> Corners_;
    std::vector<NavRouteTileDependency> Tiles_;
    std::vector<NavRouteLinkDependency> Links_;
    std::size_t StepCount_ = 0;
    std::size_t CornerCount_ = 0;
    std::size_t TileCount_ = 0;
    std::size_t LinkCount_ = 0;
};

// A region reachable from a query's start, and the travel cost at which the
// search first entered it. The cost is to the region's entry, not to any
// particular point inside it.
struct NavReachableRegion
{
    NavRegion Region;
    float EntryCost = 0.0f;
    GameplayTagId Area;
};

class NavReachableBuffer
{
public:
    explicit NavReachableBuffer(std::size_t capacity = 256);

    [[nodiscard]] std::span<const NavReachableRegion> Regions() const
    {
        return { Regions_.data(), Count_ };
    }
    void Clear() { Count_ = 0; }
    [[nodiscard]] bool Push(const NavReachableRegion& region);

private:
    std::vector<NavReachableRegion> Regions_;
    std::size_t Count_ = 0;
};

// What a query saw, for answering "why can this agent not get there?".
// Filled only when the caller passes one.
struct NavQueryDiagnostics
{
    NavStatus Status = NavStatus::Success;
    ZoneId Zone;
    GameplayTagId Profile;
    NavLocation Start;
    NavLocation End;
    std::size_t ForbiddenAreaCount = 0;
    std::size_t CapabilityCount = 0;
    std::size_t AvoidedLinkCount = 0;
    std::size_t NodesVisited = 0;
    bool SearchExhausted = false;
    float Cost = 0.0f;
    std::size_t LinksTraversed = 0;
};

struct NavCostResult
{
    NavStatus Status = NavStatus::NoPath;
    float Cost = 0.0f;
};

struct NavRaycastResult
{
    NavStatus Status = NavStatus::NoPath;
    // True when walkable surface ends before the target. This is a walkability
    // test across the navmesh, not line of sight: use PhysicsQueries for that.
    bool Hit = false;
    // Fraction of the way to the target where walkable surface ends; 1 if not.
    float Fraction = 1.0f;
    Vec3d Point = Vec3d::Zero();
    Vec3d Normal = Vec3d::Zero();
};

struct NavProjectResult
{
    NavStatus Status = NavStatus::InvalidStart;
    NavLocation Location;
};

enum class NavRouteValidity : std::uint8_t
{
    Valid,
    ZoneUnavailable,
    // The zone's navigation was unloaded and loaded again.
    ZoneReloaded,
    // A tile the route passes over was rebuilt.
    TileChanged,
    // A link the route crosses was disabled, re-costed, or re-projected.
    LinkChanged,
};
