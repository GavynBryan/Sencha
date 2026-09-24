#pragma once

#include "NavQueryPlan.h"

#include <navigation/NavigationTypes.h>

#include <cstddef>
#include <cstdint>

// Where a search starts or ends: a polygon and a point on it.
struct NavSearchPoint
{
    std::uint64_t Polygon = 0;
    float Position[3] = {};
};

struct NavSearchOutcome
{
    NavStatus Status = NavStatus::NoPath;
    // The goal's node in the context, when a goal search succeeded.
    std::int32_t GoalNode = -1;
    std::size_t NodesVisited = 0;
    bool NodeBudgetExhausted = false;
};

// Cheapest-cost search over a zone's polygons and crossable links, in the
// query context's scratch. Edges are costed between polygon-edge midpoints,
// weighted by area cost; ties break by (estimate, cost, polygon).
class NavSearch
{
public:
    NavSearch(const NavQueryPlan& plan, NavQueryContext::Backend& context)
        : Plan(plan), Context(context) {}

    // A* to `goal`, or Dijkstra when the plan says distance does not bound cost.
    [[nodiscard]] NavSearchOutcome ToGoal(const NavSearchPoint& start, const NavSearchPoint& goal);

    // Dijkstra from `start` over everything reachable within `maxCost`,
    // reporting each settled region whose entry lies within `radius`.
    [[nodiscard]] NavSearchOutcome Flood(const NavSearchPoint& start, float maxCost, float radius,
                                         NavReachableBuffer& out);

private:
    struct Target
    {
        std::uint64_t Polygon = 0;
        float Position[3] = {};
        bool Exists = false;
        bool UseHeuristic = false;
    };

    NavSearchOutcome Run(const NavSearchPoint& start);
    void ExpandWalk(const NavSearchNode& node);
    void ExpandLinks(const NavSearchNode& node);
    // Offers a cheaper way into `polygon` at `position`.
    void Relax(std::uint64_t polygon, const float* position, float cost, std::int32_t parent,
               std::uint32_t viaLink, bool viaReversed);
    [[nodiscard]] float Heuristic(const float* position) const;
    // Records a settled node as a reachable region; false when output is full.
    [[nodiscard]] bool ReportRegion(const NavSearchNode& node);

    const NavQueryPlan& Plan;
    NavQueryContext::Backend& Context;
    Target Goal;
    float FloodMaxCost = 0.0f;
    float FloodRadiusSquared = 0.0f;
    float FloodOrigin[3] = {};
    NavReachableBuffer* FloodOut = nullptr;
    NavSearchOutcome Outcome;
};
