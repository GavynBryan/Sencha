#pragma once

#include "NavQueryContextBackend.h"
#include "NavTileMeshBackend.h"
#include "ZoneNavigationBackend.h"

#include <navigation/NavigationTypes.h>

#include <cstdint>

// A request compiled against one zone and profile: per-area costs and
// exclusions, and which link kinds the agent may cross at what price. Built
// once per query, so the search itself does table lookups only.
struct NavQueryPlan
{
    const ZoneNavigation::Backend* Zone = nullptr;
    const ZoneNavProfile* Profile = nullptr;
    std::uint16_t ProfileIndex = 0;
    const dtNavMesh* Mesh = nullptr;
    const NavQueryRequest* Request = nullptr;

    float AreaCost[64] = {};
    bool AreaForbidden[64] = {};
    // Cheapest ground the agent may walk; scales the distance heuristic.
    float CheapestAreaCost = 1.0f;
    // Bit k: the agent may cross links of traversal kind k.
    std::uint64_t CrossableKinds = 0;
    float KindCostMultiplier[64] = {};
    float KindCostAdd[64] = {};

    [[nodiscard]] std::uint8_t AreaOf(std::uint64_t polygon) const;
    [[nodiscard]] bool IsCurrent(const NavLocation& location) const;
    // Whether this query may cross link `index`, and its cost if so.
    [[nodiscard]] bool CanCross(std::uint32_t index, float& cost) const;
    // Whether straight-line distance times the cheapest ground bounds every
    // route. A crossable link cheaper than walking its own span (a teleport)
    // breaks that, and the search must run without a heuristic.
    [[nodiscard]] bool DistanceBoundsCost() const;
};

// Compiles `request` against `zone` and binds the context's Detour query to
// the profile's mesh. The context's area filter reads the plan, so the plan
// must outlive the query it serves.
[[nodiscard]] NavStatus PlanNavQuery(const ZoneNavigation& zone, NavQueryContext::Backend& context,
                                     const NavQueryRequest& request, NavQueryPlan& plan);
