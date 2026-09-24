#include "NavSearch.h"

#include <DetourCommon.h>

#include <algorithm>
#include <cmath>

namespace
{
    // Slightly under the true bound, so rounding never makes it inadmissible.
    constexpr float kHeuristicScale = 0.999f;

    // Midpoint of the edge a polygon link crosses, clamped to the part the
    // neighbour shares at a tile border.
    void SharedEdgeMidpoint(const dtMeshTile* tile, const dtPoly* poly, const dtLink& link,
                            float* mid)
    {
        const float* a = &tile->verts[poly->verts[link.edge] * 3];
        const float* b = &tile->verts[poly->verts[(link.edge + 1) % poly->vertCount] * 3];
        float from = 0.0f;
        float to = 1.0f;
        if (link.side != 0xff && (link.bmin != 0 || link.bmax != 255))
        {
            from = static_cast<float>(link.bmin) / 255.0f;
            to = static_cast<float>(link.bmax) / 255.0f;
        }
        dtVlerp(mid, a, b, (from + to) * 0.5f);
    }
}

NavSearchOutcome NavSearch::ToGoal(const NavSearchPoint& start, const NavSearchPoint& goal)
{
    Goal.Polygon = goal.Polygon;
    dtVcopy(Goal.Position, goal.Position);
    Goal.Exists = true;
    Goal.UseHeuristic = Plan.DistanceBoundsCost();
    return Run(start);
}

NavSearchOutcome NavSearch::Flood(const NavSearchPoint& start, float maxCost, float radius,
                                  NavReachableBuffer& out)
{
    FloodMaxCost = maxCost;
    FloodRadiusSquared = radius * radius;
    dtVcopy(FloodOrigin, start.Position);
    FloodOut = &out;
    return Run(start);
}

float NavSearch::Heuristic(const float* position) const
{
    return Goal.UseHeuristic
        ? dtVdist(position, Goal.Position) * Plan.CheapestAreaCost * kHeuristicScale
        : 0.0f;
}

NavSearchOutcome NavSearch::Run(const NavSearchPoint& start)
{
    Outcome = NavSearchOutcome{};
    Context.ResetSearch();
    NavSearchNode& first = *Context.AddNode(start.Polygon);
    dtVcopy(first.Position, start.Position);
    if (Goal.Exists && start.Polygon == Goal.Polygon)
        first.Cost = dtVdist(start.Position, Goal.Position)
                   * Plan.AreaCost[Plan.AreaOf(start.Polygon)];
    first.Estimate = first.Cost + (start.Polygon == Goal.Polygon ? 0.0f : Heuristic(start.Position));
    Context.HeapPush(0);

    while (Context.HeapSize > 0)
    {
        const NavSearchNode& node = Context.At(Context.HeapPop());
        ++Outcome.NodesVisited;
        if (Goal.Exists && node.Polygon == Goal.Polygon)
        {
            Outcome.GoalNode = Context.IndexOf(node);
            Outcome.Status = NavStatus::Success;
            return Outcome;
        }
        if (!Goal.Exists && !ReportRegion(node))
        {
            Outcome.Status = NavStatus::OutputCapacityReached;
            return Outcome;
        }
        ExpandWalk(node);
        ExpandLinks(node);
    }

    if (Outcome.NodeBudgetExhausted)
        Outcome.Status = NavStatus::SearchLimitReached;
    else
        Outcome.Status = Goal.Exists ? NavStatus::NoPath : NavStatus::Success;
    return Outcome;
}

bool NavSearch::ReportRegion(const NavSearchNode& node)
{
    if (dtVdistSqr(node.Position, FloodOrigin) > FloodRadiusSquared)
        return true;
    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    Plan.Mesh->getTileAndPolyByRefUnsafe(node.Polygon, &tile, &poly);
    const std::uint8_t area = poly->getArea();
    NavReachableRegion region;
    region.Region = NavRegion{ Plan.Zone->Zone, Plan.Zone->Generation, Plan.ProfileIndex, 0,
                               node.Polygon };
    region.EntryCost = node.Cost;
    region.Area = area < Plan.Zone->AreaTags.size() ? Plan.Zone->AreaTags[area] : GameplayTagId{};
    return FloodOut->Push(region);
}

void NavSearch::ExpandWalk(const NavSearchNode& node)
{
    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    Plan.Mesh->getTileAndPolyByRefUnsafe(node.Polygon, &tile, &poly);
    const float areaCost = Plan.AreaCost[poly->getArea()];
    const std::uint64_t parentPolygon = node.Parent >= 0 ? Context.At(node.Parent).Polygon : 0;
    const std::int32_t index = Context.IndexOf(node);

    for (unsigned int i = poly->firstLink; i != DT_NULL_LINK; i = tile->links[i].next)
    {
        const dtLink& link = tile->links[i];
        if (link.ref == 0 || link.ref == parentPolygon)
            continue;
        const dtMeshTile* neighbourTile = nullptr;
        const dtPoly* neighbour = nullptr;
        Plan.Mesh->getTileAndPolyByRefUnsafe(link.ref, &neighbourTile, &neighbour);
        if (Plan.AreaForbidden[neighbour->getArea()])
            continue;
        float mid[3];
        SharedEdgeMidpoint(tile, poly, link, mid);
        float cost = node.Cost + dtVdist(node.Position, mid) * areaCost;
        if (Goal.Exists && link.ref == Goal.Polygon)
            cost += dtVdist(mid, Goal.Position) * Plan.AreaCost[neighbour->getArea()];
        Relax(link.ref, mid, cost, index, NavSearchNode::kNoLink, false);
    }
}

void NavSearch::ExpandLinks(const NavSearchNode& node)
{
    const float areaCost = Plan.AreaCost[Plan.AreaOf(node.Polygon)];
    const std::int32_t index = Context.IndexOf(node);
    const std::vector<NavLinkEdge>& edges = Plan.Profile->Edges;
    auto edge = std::ranges::lower_bound(edges, node.Polygon, {}, &NavLinkEdge::From);
    for (; edge != edges.end() && edge->From == node.Polygon; ++edge)
    {
        float linkCost = 0.0f;
        if (!Plan.CanCross(edge->Link, linkCost))
            continue;
        const NavLinkEndpoints& ends = Plan.Profile->Endpoints[edge->Link];
        const NavAnchorAttachment& from = edge->Forward ? ends.Entry : ends.Exit;
        const NavAnchorAttachment& to = edge->Forward ? ends.Exit : ends.Entry;
        const std::uint8_t landingArea = Plan.AreaOf(to.Polygon);
        if (Plan.AreaForbidden[landingArea])
            continue;
        const float landingCost = Plan.AreaCost[landingArea];
        float fromPos[3];
        float toPos[3];
        CopyToDetour(from.Position, fromPos);
        CopyToDetour(to.Position, toPos);
        float cost = node.Cost + dtVdist(node.Position, fromPos) * areaCost + linkCost;
        if (Goal.Exists && to.Polygon == Goal.Polygon)
            cost += dtVdist(toPos, Goal.Position) * landingCost;
        Relax(to.Polygon, toPos, cost, index, edge->Link, !edge->Forward);
    }
}

void NavSearch::Relax(std::uint64_t polygon, const float* position, float cost,
                      std::int32_t parent, std::uint32_t viaLink, bool viaReversed)
{
    if (!Goal.Exists && cost > FloodMaxCost)
        return;
    NavSearchNode* node = Context.FindNode(polygon);
    if (node == nullptr)
    {
        node = Context.AddNode(polygon);
        if (node == nullptr)
        {
            Outcome.NodeBudgetExhausted = true;
            return;
        }
    }
    else if (cost >= node->Cost)
    {
        return;
    }
    dtVcopy(node->Position, position);
    node->Cost = cost;
    node->Estimate = cost + (polygon == Goal.Polygon ? 0.0f : Heuristic(position));
    node->Parent = parent;
    node->ViaLink = viaLink;
    node->ViaReversed = viaReversed;
    if (node->HeapIndex >= 0)
        Context.HeapUpdate(Context.IndexOf(*node));
    else
        Context.HeapPush(Context.IndexOf(*node));
}
