#pragma once

#include <math/geometry/3d/Aabb3d.h>
#include <navigation/NavQueryContext.h>
#include <navigation/NavigationTypes.h>

#include <optional>

class RuntimeWorld;
class ZoneNavigation;

// Read-only questions about one zone's walkable space. Each operation takes a
// caller-owned NavQueryContext and allocates nothing once it is sized; distinct
// contexts may query one zone concurrently. Results are deterministic for
// identical navigation, link state, and request. Costs and guarantees:
// docs/navigation/runtime.md.

// The walkable point nearest `point` within `halfExtents`.
[[nodiscard]] NavProjectResult NavProjectPoint(const ZoneNavigation& zone,
                                               NavQueryContext& context,
                                               const NavQueryRequest& request,
                                               const Vec3d& point,
                                               const Vec3d& halfExtents);

// Whether walkable surface continues in a straight line from `start` toward
// `target`. Not line of sight -- use PhysicsQueries::Raycast for that.
[[nodiscard]] NavRaycastResult NavRaycast(const ZoneNavigation& zone,
                                          NavQueryContext& context,
                                          const NavQueryRequest& request,
                                          const NavLocation& start,
                                          const Vec3d& target);

[[nodiscard]] NavStatus NavReachable(const ZoneNavigation& zone,
                                     NavQueryContext& context,
                                     const NavQueryRequest& request,
                                     const NavLocation& start,
                                     const NavLocation& end,
                                     NavQueryDiagnostics* diagnostics = nullptr);

// The cheapest route's cost, without assembling the route.
[[nodiscard]] NavCostResult NavTravelCost(const ZoneNavigation& zone,
                                          NavQueryContext& context,
                                          const NavQueryRequest& request,
                                          const NavLocation& start,
                                          const NavLocation& end,
                                          NavQueryDiagnostics* diagnostics = nullptr);

// The cheapest route, each link crossing its own step.
[[nodiscard]] NavStatus NavFindRoute(const ZoneNavigation& zone,
                                     NavQueryContext& context,
                                     const NavQueryRequest& request,
                                     const NavLocation& start,
                                     const NavLocation& end,
                                     NavRouteBuffer& route,
                                     NavQueryDiagnostics* diagnostics = nullptr);

// Regions reachable from `start` for at most `maxCost`, cheapest first, keeping
// those whose entry point is within `radius`. The radius filters the output,
// not the search, so entry costs are true costs. Makes no sampling decision.
[[nodiscard]] NavStatus NavCollectReachable(const ZoneNavigation& zone,
                                            NavQueryContext& context,
                                            const NavQueryRequest& request,
                                            const NavLocation& start,
                                            float radius,
                                            float maxCost,
                                            NavReachableBuffer& out);

[[nodiscard]] NavProjectResult NavClosestPointInRegion(const ZoneNavigation& zone,
                                                       NavQueryContext& context,
                                                       const NavRegion& region,
                                                       const Vec3d& point);

// Nullopt when the region is stale.
[[nodiscard]] std::optional<Aabb3d> NavRegionBounds(const ZoneNavigation& zone,
                                                    const NavRegion& region);

// Whether a stored route still holds. Only what it depends on can invalidate
// it: its zone reloading, a tile it crosses rebuilding, or a link it uses
// changing.
[[nodiscard]] NavRouteValidity NavValidateRoute(const ZoneNavigation& zone,
                                                const NavRouteBuffer& route);

// The same operations, finding the request's zone among a runtime world's
// resident zones (dormant included); ZoneUnavailable otherwise.
class NavigationQuery
{
public:
    explicit NavigationQuery(const RuntimeWorld& world) : World(&world) {}

    [[nodiscard]] const ZoneNavigation* FindZone(ZoneId zone) const;

    [[nodiscard]] NavProjectResult ProjectPoint(NavQueryContext& context,
                                                const NavQueryRequest& request,
                                                const Vec3d& point,
                                                const Vec3d& halfExtents) const;
    [[nodiscard]] NavRaycastResult Raycast(NavQueryContext& context,
                                           const NavQueryRequest& request,
                                           const NavLocation& start,
                                           const Vec3d& target) const;
    [[nodiscard]] NavStatus Reachable(NavQueryContext& context,
                                      const NavQueryRequest& request,
                                      const NavLocation& start,
                                      const NavLocation& end,
                                      NavQueryDiagnostics* diagnostics = nullptr) const;
    [[nodiscard]] NavCostResult TravelCost(NavQueryContext& context,
                                           const NavQueryRequest& request,
                                           const NavLocation& start,
                                           const NavLocation& end,
                                           NavQueryDiagnostics* diagnostics = nullptr) const;
    [[nodiscard]] NavStatus FindRoute(NavQueryContext& context,
                                      const NavQueryRequest& request,
                                      const NavLocation& start,
                                      const NavLocation& end,
                                      NavRouteBuffer& route,
                                      NavQueryDiagnostics* diagnostics = nullptr) const;
    [[nodiscard]] NavStatus CollectReachable(NavQueryContext& context,
                                             const NavQueryRequest& request,
                                             const NavLocation& start,
                                             float radius,
                                             float maxCost,
                                             NavReachableBuffer& out) const;
    [[nodiscard]] NavRouteValidity ValidateRoute(const NavRouteBuffer& route) const;

private:
    template <typename Result, typename Operation>
    Result InZone(ZoneId zone, Result unavailable, Operation&& operation) const
    {
        const ZoneNavigation* navigation = FindZone(zone);
        return navigation != nullptr ? operation(*navigation) : unavailable;
    }

    const RuntimeWorld* World;
};
