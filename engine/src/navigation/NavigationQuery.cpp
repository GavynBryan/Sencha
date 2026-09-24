#include <navigation/NavigationQuery.h>

#include "NavQueryContextBackend.h"
#include "NavQueryPlan.h"
#include "NavSearch.h"
#include "NavTileMeshBackend.h"
#include "ZoneNavigationBackend.h"

#include <navigation/NavTileMesh.h>
#include <navigation/ZoneNavigation.h>
#include <world/RuntimeWorld.h>

#include <DetourCommon.h>

#include <algorithm>
#include <cmath>

namespace
{
    using Context = NavQueryContext::Backend;

    Vec3d FromFloats(const float* v) { return Vec3d(v[0], v[1], v[2]); }

    NavSearchPoint PointOf(const NavLocation& location)
    {
        NavSearchPoint point;
        point.Polygon = location.Ref;
        CopyToDetour(location.Position, point.Position);
        return point;
    }

    NavLocation LocationIn(const NavQueryPlan& plan, std::uint64_t polygon, const float* position)
    {
        return NavLocation{ plan.Zone->Zone, plan.Zone->Generation, plan.ProfileIndex, 0,
                            polygon, FromFloats(position) };
    }

    // An endpoint the plan can search from: present, and made against the
    // navigation the plan reads.
    NavStatus CheckEndpoint(const NavQueryPlan& plan, const NavLocation& location,
                            NavStatus whenMissing)
    {
        if (!location.IsValid())
            return whenMissing;
        return plan.IsCurrent(location) ? NavStatus::Success : NavStatus::StaleLocation;
    }

    void BeginDiagnostics(NavQueryDiagnostics* diagnostics, const ZoneNavigation& zone,
                          const NavQueryRequest& request, const NavLocation& start,
                          const NavLocation& end)
    {
        if (diagnostics == nullptr)
            return;
        *diagnostics = NavQueryDiagnostics{};
        diagnostics->Zone = zone.Zone();
        diagnostics->Profile = request.Profile;
        diagnostics->Start = start;
        diagnostics->End = end;
        diagnostics->CapabilityCount = request.Capabilities.size();
        diagnostics->AvoidedLinkCount = request.AvoidLinks.size();
        if (request.Policy != nullptr)
            diagnostics->ForbiddenAreaCount = request.Policy->ForbiddenAreas.size();
    }

    // Plans the request, validates both endpoints, and searches between them.
    NavStatus SearchBetween(const ZoneNavigation& zone, Context& context,
                            const NavQueryRequest& request, const NavLocation& start,
                            const NavLocation& end, NavQueryPlan& plan, NavSearchOutcome& outcome,
                            NavQueryDiagnostics* diagnostics)
    {
        BeginDiagnostics(diagnostics, zone, request, start, end);
        const auto finish = [diagnostics](NavStatus status)
        {
            if (diagnostics != nullptr)
                diagnostics->Status = status;
            return status;
        };
        if (start.Zone != end.Zone)
            return finish(NavStatus::CrossZoneUnsupported);
        NavStatus status = PlanNavQuery(zone, context, request, plan);
        if (status == NavStatus::Success)
            status = CheckEndpoint(plan, start, NavStatus::InvalidStart);
        if (status == NavStatus::Success)
            status = CheckEndpoint(plan, end, NavStatus::InvalidDestination);
        if (status != NavStatus::Success)
            return finish(status);

        outcome = NavSearch(plan, context).ToGoal(PointOf(start), PointOf(end));
        if (diagnostics != nullptr)
        {
            diagnostics->NodesVisited = outcome.NodesVisited;
            diagnostics->SearchExhausted = outcome.NodeBudgetExhausted;
            if (outcome.GoalNode >= 0)
                diagnostics->Cost = context.At(outcome.GoalNode).Cost;
        }
        return finish(outcome.Status);
    }

    // Turns a goal search's node chain into walk legs and link crossings.
    class RouteAssembler
    {
    public:
        RouteAssembler(const NavQueryPlan& plan, Context& context, NavRouteBuffer& route)
            : Plan(plan), Ctx(context), Route(route) {}

        NavStatus Assemble(const NavLocation& start, const NavLocation& end, std::int32_t goal)
        {
            std::size_t length = 0;
            for (std::int32_t n = goal; n >= 0; n = Ctx.At(n).Parent)
                Ctx.Chain[length++] = n;
            std::reverse(Ctx.Chain.begin(), Ctx.Chain.begin() + static_cast<std::ptrdiff_t>(length));

            CopyToDetour(start.Position, LegStart);
            for (std::size_t k = 0; k < length; ++k)
            {
                const NavSearchNode& node = Ctx.At(Ctx.Chain[k]);
                if (k > 0 && node.ViaLink != NavSearchNode::kNoLink
                    && !CrossLink(node.ViaLink, node.ViaReversed))
                    return NavStatus::OutputCapacityReached;
                if (!EnterPolygon(node.Polygon))
                    return NavStatus::OutputCapacityReached;
            }
            float goalPosition[3];
            CopyToDetour(end.Position, goalPosition);
            return EmitWalk(goalPosition) ? NavStatus::Success : NavStatus::OutputCapacityReached;
        }

        [[nodiscard]] std::size_t LinksCrossed() const { return Crossings; }

    private:
        bool EnterPolygon(std::uint64_t polygon)
        {
            if (CorridorLength >= Ctx.Corridor.size())
                return false;
            Ctx.Corridor[CorridorLength++] = polygon;
            const dtMeshTile* tile = nullptr;
            const dtPoly* poly = nullptr;
            Plan.Mesh->getTileAndPolyByRefUnsafe(polygon, &tile, &poly);
            return Route.AddTile(NavTileCoord{ tile->header->x, tile->header->y },
                                 Plan.Mesh->getTileRef(tile));
        }

        bool CrossLink(std::uint32_t linkIndex, bool reversed)
        {
            const NavLinkEndpoints& ends = Plan.Profile->Endpoints[linkIndex];
            const NavAnchorAttachment& from = reversed ? ends.Exit : ends.Entry;
            const NavAnchorAttachment& to = reversed ? ends.Entry : ends.Exit;
            float entry[3];
            CopyToDetour(from.Position, entry);
            if (!EmitWalk(entry))
                return false;

            const NavLinkInfo& link = Plan.Zone->Links[linkIndex];
            NavRouteStep* step = Route.PushStep();
            if (step == nullptr || !Route.AddLink(link.Id, link.Revision))
                return false;
            float cost = 0.0f;
            (void)Plan.CanCross(linkIndex, cost);
            step->Kind = NavRouteStepKind::Traverse;
            step->Link = link.Id;
            step->Traversal = link.Traversal;
            step->Entry = from.Position;
            step->Exit = to.Position;
            step->EntryRadius = link.EntryRadius;
            step->Reversed = reversed;
            step->Cost = cost;
            ++Crossings;
            CorridorLength = 0;
            CopyToDetour(to.Position, LegStart);
            return true;
        }

        // Emits the current corridor as one walk step ending at `legEnd`.
        bool EmitWalk(const float* legEnd)
        {
            if (CorridorLength == 0)
                return true;
            int count = 0;
            if (dtStatusFailed(Ctx.Query->findStraightPath(
                    LegStart, legEnd, Ctx.Corridor.data(), static_cast<int>(CorridorLength),
                    Ctx.StraightPath.data(), Ctx.StraightFlags.data(), Ctx.StraightRefs.data(),
                    &count, static_cast<int>(Ctx.Config.MaxCorridor), 0))
                || count < 2)
                return true;
            NavRouteStep* step = Route.PushStep();
            if (step == nullptr)
                return false;
            step->Kind = NavRouteStepKind::Walk;
            step->FirstCorner = static_cast<std::uint32_t>(Route.CornerCount());
            step->CornerCount = static_cast<std::uint32_t>(count - 1);
            // The first straight-path point is the leg start; corners follow it.
            const float* previous = LegStart;
            for (int i = 1; i < count; ++i)
            {
                const float* corner = &Ctx.StraightPath[static_cast<std::size_t>(i) * 3];
                if (!Route.PushCorner(FromFloats(corner)))
                    return false;
                Route.WalkDistance += dtVdist(previous, corner);
                previous = corner;
            }
            return true;
        }

        const NavQueryPlan& Plan;
        Context& Ctx;
        NavRouteBuffer& Route;
        float LegStart[3] = {};
        std::size_t CorridorLength = 0;
        std::size_t Crossings = 0;
    };

    bool RegionIsCurrent(const ZoneNavigation::Backend& zone, const NavRegion& region)
    {
        if (region.Zone != zone.Zone || region.Generation != zone.Generation
            || region.Profile >= zone.Profiles.size() || region.Ref == 0)
            return false;
        const dtNavMesh* mesh = zone.Profiles[region.Profile].Mesh.GetBackend().Mesh;
        return mesh != nullptr && mesh->isValidPolyRef(region.Ref);
    }
}

NavProjectResult NavProjectPoint(const ZoneNavigation& zone, NavQueryContext& context,
                                 const NavQueryRequest& request, const Vec3d& point,
                                 const Vec3d& halfExtents)
{
    Context& ctx = context.GetBackend();
    NavQueryPlan plan;
    if (const NavStatus status = PlanNavQuery(zone, ctx, request, plan); status != NavStatus::Success)
        return NavProjectResult{ status, {} };
    float center[3];
    float extents[3];
    CopyToDetour(point, center);
    CopyToDetour(halfExtents, extents);
    dtPolyRef polygon = 0;
    float nearest[3] = {};
    if (dtStatusFailed(ctx.Query->findNearestPoly(center, extents, &ctx.Filter, &polygon, nearest))
        || polygon == 0)
        return NavProjectResult{ NavStatus::InvalidStart, {} };
    return NavProjectResult{ NavStatus::Success, LocationIn(plan, polygon, nearest) };
}

NavRaycastResult NavRaycast(const ZoneNavigation& zone, NavQueryContext& context,
                            const NavQueryRequest& request, const NavLocation& start,
                            const Vec3d& target)
{
    NavRaycastResult result;
    Context& ctx = context.GetBackend();
    NavQueryPlan plan;
    result.Status = PlanNavQuery(zone, ctx, request, plan);
    if (result.Status == NavStatus::Success)
        result.Status = CheckEndpoint(plan, start, NavStatus::InvalidStart);
    if (result.Status != NavStatus::Success)
        return result;

    float from[3];
    float to[3];
    CopyToDetour(start.Position, from);
    CopyToDetour(target, to);
    float hitFraction = 0.0f;
    float normal[3] = {};
    int pathCount = 0;
    if (dtStatusFailed(ctx.Query->raycast(start.Ref, from, to, &ctx.Filter, &hitFraction, normal,
                                          ctx.RaycastPath.data(), &pathCount,
                                          static_cast<int>(ctx.Config.MaxCorridor))))
    {
        result.Status = NavStatus::InvalidStart;
        return result;
    }
    // Detour reports FLT_MAX when the ray reaches its end.
    result.Hit = hitFraction <= 1.0f;
    result.Fraction = result.Hit ? hitFraction : 1.0f;
    result.Point = start.Position + (target - start.Position) * result.Fraction;
    result.Normal = FromFloats(normal);
    return result;
}

NavStatus NavReachable(const ZoneNavigation& zone, NavQueryContext& context,
                       const NavQueryRequest& request, const NavLocation& start,
                       const NavLocation& end, NavQueryDiagnostics* diagnostics)
{
    NavQueryPlan plan;
    NavSearchOutcome outcome;
    return SearchBetween(zone, context.GetBackend(), request, start, end, plan, outcome,
                         diagnostics);
}

NavCostResult NavTravelCost(const ZoneNavigation& zone, NavQueryContext& context,
                            const NavQueryRequest& request, const NavLocation& start,
                            const NavLocation& end, NavQueryDiagnostics* diagnostics)
{
    Context& ctx = context.GetBackend();
    NavQueryPlan plan;
    NavSearchOutcome outcome;
    NavCostResult result;
    result.Status = SearchBetween(zone, ctx, request, start, end, plan, outcome, diagnostics);
    if (result.Status == NavStatus::Success)
        result.Cost = ctx.At(outcome.GoalNode).Cost;
    return result;
}

NavStatus NavFindRoute(const ZoneNavigation& zone, NavQueryContext& context,
                       const NavQueryRequest& request, const NavLocation& start,
                       const NavLocation& end, NavRouteBuffer& route,
                       NavQueryDiagnostics* diagnostics)
{
    route.Clear();
    Context& ctx = context.GetBackend();
    NavQueryPlan plan;
    NavSearchOutcome outcome;
    NavStatus status = SearchBetween(zone, ctx, request, start, end, plan, outcome, diagnostics);
    if (status != NavStatus::Success)
        return status;

    route.Zone = plan.Zone->Zone;
    route.Generation = plan.Zone->Generation;
    route.Profile = plan.ProfileIndex;
    route.Cost = ctx.At(outcome.GoalNode).Cost;
    RouteAssembler assembler(plan, ctx, route);
    status = assembler.Assemble(start, end, outcome.GoalNode);
    if (status != NavStatus::Success)
        route.Clear();
    if (diagnostics != nullptr)
    {
        diagnostics->Status = status;
        diagnostics->LinksTraversed = assembler.LinksCrossed();
    }
    return status;
}

NavStatus NavCollectReachable(const ZoneNavigation& zone, NavQueryContext& context,
                              const NavQueryRequest& request, const NavLocation& start,
                              float radius, float maxCost, NavReachableBuffer& out)
{
    out.Clear();
    Context& ctx = context.GetBackend();
    NavQueryPlan plan;
    NavStatus status = PlanNavQuery(zone, ctx, request, plan);
    if (status == NavStatus::Success)
        status = CheckEndpoint(plan, start, NavStatus::InvalidStart);
    if (status != NavStatus::Success)
        return status;
    return NavSearch(plan, ctx)
        .Flood(PointOf(start), std::max(maxCost, 0.0f), std::max(radius, 0.0f), out)
        .Status;
}

NavProjectResult NavClosestPointInRegion(const ZoneNavigation& zone, NavQueryContext& context,
                                         const NavRegion& region, const Vec3d& point)
{
    const ZoneNavigation::Backend& backend = zone.GetBackend();
    if (!RegionIsCurrent(backend, region))
        return NavProjectResult{ NavStatus::StaleLocation, {} };
    Context& ctx = context.GetBackend();
    const dtNavMesh* mesh = backend.Profiles[region.Profile].Mesh.GetBackend().Mesh;
    if (ctx.Query == nullptr
        || dtStatusFailed(ctx.Query->init(mesh, static_cast<int>(ctx.Config.MaxNodes))))
        return NavProjectResult{ NavStatus::SearchLimitReached, {} };
    float position[3];
    float closest[3] = {};
    CopyToDetour(point, position);
    if (dtStatusFailed(ctx.Query->closestPointOnPoly(region.Ref, position, closest, nullptr)))
        return NavProjectResult{ NavStatus::StaleLocation, {} };
    return NavProjectResult{ NavStatus::Success,
                             NavLocation{ region.Zone, region.Generation, region.Profile, 0,
                                          region.Ref, FromFloats(closest) } };
}

std::optional<Aabb3d> NavRegionBounds(const ZoneNavigation& zone, const NavRegion& region)
{
    const ZoneNavigation::Backend& backend = zone.GetBackend();
    if (!RegionIsCurrent(backend, region))
        return std::nullopt;
    const dtNavMesh* mesh = backend.Profiles[region.Profile].Mesh.GetBackend().Mesh;
    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    mesh->getTileAndPolyByRefUnsafe(region.Ref, &tile, &poly);
    Aabb3d bounds = Aabb3d::Empty();
    for (unsigned int i = 0; i < poly->vertCount; ++i)
        bounds.ExpandToInclude(FromFloats(&tile->verts[poly->verts[i] * 3]));
    return bounds;
}

NavRouteValidity NavValidateRoute(const ZoneNavigation& zone, const NavRouteBuffer& route)
{
    if (route.Zone != zone.Zone() || route.Generation != zone.Generation()
        || route.Profile >= zone.ProfileCount())
        return NavRouteValidity::ZoneReloaded;
    const NavTileMesh& mesh = zone.Mesh(route.Profile);
    for (const NavRouteTileDependency& tile : route.TileDependencies())
        if (mesh.TileRevision(tile.Tile) != tile.Revision)
            return NavRouteValidity::TileChanged;
    for (const NavRouteLinkDependency& link : route.LinkDependencies())
    {
        const std::optional<std::uint32_t> index = zone.FindLink(link.Link);
        if (!index || zone.Link(*index).Revision != link.Revision)
            return NavRouteValidity::LinkChanged;
    }
    return NavRouteValidity::Valid;
}

const ZoneNavigation* NavigationQuery::FindZone(ZoneId zone) const
{
    return FindZoneNavigation(*World, zone);
}

NavProjectResult NavigationQuery::ProjectPoint(NavQueryContext& context,
                                               const NavQueryRequest& request,
                                               const Vec3d& point,
                                               const Vec3d& halfExtents) const
{
    return InZone(request.Zone, NavProjectResult{ NavStatus::ZoneUnavailable, {} },
                  [&](const ZoneNavigation& zone)
                  { return NavProjectPoint(zone, context, request, point, halfExtents); });
}

NavRaycastResult NavigationQuery::Raycast(NavQueryContext& context,
                                          const NavQueryRequest& request,
                                          const NavLocation& start,
                                          const Vec3d& target) const
{
    NavRaycastResult unavailable;
    unavailable.Status = NavStatus::ZoneUnavailable;
    return InZone(request.Zone, unavailable, [&](const ZoneNavigation& zone)
                  { return NavRaycast(zone, context, request, start, target); });
}

NavStatus NavigationQuery::Reachable(NavQueryContext& context, const NavQueryRequest& request,
                                     const NavLocation& start, const NavLocation& end,
                                     NavQueryDiagnostics* diagnostics) const
{
    return InZone(request.Zone, NavStatus::ZoneUnavailable, [&](const ZoneNavigation& zone)
                  { return NavReachable(zone, context, request, start, end, diagnostics); });
}

NavCostResult NavigationQuery::TravelCost(NavQueryContext& context,
                                          const NavQueryRequest& request,
                                          const NavLocation& start, const NavLocation& end,
                                          NavQueryDiagnostics* diagnostics) const
{
    return InZone(request.Zone, NavCostResult{ NavStatus::ZoneUnavailable, 0.0f },
                  [&](const ZoneNavigation& zone)
                  { return NavTravelCost(zone, context, request, start, end, diagnostics); });
}

NavStatus NavigationQuery::FindRoute(NavQueryContext& context, const NavQueryRequest& request,
                                     const NavLocation& start, const NavLocation& end,
                                     NavRouteBuffer& route,
                                     NavQueryDiagnostics* diagnostics) const
{
    route.Clear();
    return InZone(request.Zone, NavStatus::ZoneUnavailable, [&](const ZoneNavigation& zone)
                  { return NavFindRoute(zone, context, request, start, end, route, diagnostics); });
}

NavStatus NavigationQuery::CollectReachable(NavQueryContext& context,
                                            const NavQueryRequest& request,
                                            const NavLocation& start, float radius,
                                            float maxCost, NavReachableBuffer& out) const
{
    out.Clear();
    return InZone(request.Zone, NavStatus::ZoneUnavailable, [&](const ZoneNavigation& zone)
                  { return NavCollectReachable(zone, context, request, start, radius, maxCost, out); });
}

NavRouteValidity NavigationQuery::ValidateRoute(const NavRouteBuffer& route) const
{
    return InZone(route.Zone, NavRouteValidity::ZoneUnavailable,
                  [&](const ZoneNavigation& zone) { return NavValidateRoute(zone, route); });
}
