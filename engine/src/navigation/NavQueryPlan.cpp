#include "NavQueryPlan.h"

#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace
{
    float ValidOr(float value, float fallback, float minimum)
    {
        return std::isfinite(value) && value >= minimum ? value : fallback;
    }

    template <typename Range, typename Value>
    bool Contains(const Range& range, const Value& value)
    {
        return std::ranges::find(range, value) != std::ranges::end(range);
    }

    void CompileAreas(NavQueryPlan& plan)
    {
        const NavQueryPolicy* policy = plan.Request->Policy;
        const std::span<const GameplayTagId> areas = plan.Zone->AreaTags;
        plan.CheapestAreaCost = FLT_MAX;
        for (std::size_t area = 0; area < 64; ++area)
        {
            plan.AreaCost[area] = 1.0f;
            const GameplayTagId tag = area < areas.size() ? areas[area] : GameplayTagId{};
            if (policy != nullptr && tag.IsValid())
            {
                for (const NavAreaCost& cost : policy->AreaCosts)
                    if (cost.Area == tag)
                        plan.AreaCost[area] = cost.Cost > 0.0f ? ValidOr(cost.Cost, 1.0f, 0.0f)
                                                               : 1.0f;
                plan.AreaForbidden[area] = Contains(policy->ForbiddenAreas, tag);
            }
            const bool present = area < std::max<std::size_t>(areas.size(), 1);
            if (present && !plan.AreaForbidden[area])
                plan.CheapestAreaCost = std::min(plan.CheapestAreaCost, plan.AreaCost[area]);
        }
        if (plan.CheapestAreaCost == FLT_MAX)
            plan.CheapestAreaCost = 0.0f;
    }

    bool Grants(const GameplayTagRegistry* tags, GameplayTagId capability, GameplayTagId kind)
    {
        return capability == kind
            || (tags != nullptr && capability.IsValid() && tags->IsDescendantOf(kind, capability));
    }

    void CompileTraversalKinds(NavQueryPlan& plan)
    {
        const NavQueryPolicy* policy = plan.Request->Policy;
        for (std::size_t k = 0; k < plan.Zone->TraversalKinds.size(); ++k)
        {
            const GameplayTagId kind = plan.Zone->TraversalKinds[k];
            plan.KindCostMultiplier[k] = 1.0f;
            plan.KindCostAdd[k] = 0.0f;
            if (std::ranges::any_of(plan.Request->Capabilities, [&](GameplayTagId capability)
                                    { return Grants(plan.Zone->Tags, capability, kind); }))
                plan.CrossableKinds |= std::uint64_t{ 1 } << k;
            if (policy == nullptr)
                continue;
            for (const NavTraversalCost& cost : policy->TraversalCosts)
                if (cost.Kind == kind)
                {
                    plan.KindCostMultiplier[k] = ValidOr(cost.Multiplier, 1.0f, 0.0f);
                    plan.KindCostAdd[k] = ValidOr(cost.Add, 0.0f, 0.0f);
                }
        }
    }
}

std::uint8_t NavQueryPlan::AreaOf(std::uint64_t polygon) const
{
    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    Mesh->getTileAndPolyByRefUnsafe(polygon, &tile, &poly);
    return poly->getArea();
}

bool NavQueryPlan::IsCurrent(const NavLocation& location) const
{
    return location.Zone == Zone->Zone && location.Generation == Zone->Generation
        && location.Profile == ProfileIndex && location.Ref != 0
        && Mesh->isValidPolyRef(location.Ref);
}

bool NavQueryPlan::CanCross(std::uint32_t index, float& cost) const
{
    const NavLinkInfo& link = Zone->Links[index];
    const std::uint8_t kind = Zone->LinkKindIndex[index];
    if (!link.Enabled || kind == kUnboundTraversalKind
        || (CrossableKinds & (std::uint64_t{ 1 } << kind)) == 0
        || Contains(Request->AvoidLinks, link.Id))
        return false;
    for (const NavLinkCostOverride& override : Request->LinkCostOverrides)
        if (override.Link == link.Id)
        {
            cost = ValidOr(override.Cost, link.BaseCost, 0.0f);
            return true;
        }
    cost = link.BaseCost * link.CostScale * KindCostMultiplier[kind] + KindCostAdd[kind];
    return true;
}

bool NavQueryPlan::DistanceBoundsCost() const
{
    if (CheapestAreaCost <= 0.0f)
        return false;
    for (const NavLinkEdge& edge : Profile->Edges)
    {
        float cost = 0.0f;
        if (!CanCross(edge.Link, cost))
            continue;
        const NavLinkEndpoints& ends = Profile->Endpoints[edge.Link];
        if (cost < CheapestAreaCost * (ends.Exit.Position - ends.Entry.Position).Magnitude())
            return false;
    }
    return true;
}

NavStatus PlanNavQuery(const ZoneNavigation& zone, NavQueryContext::Backend& context,
                       const NavQueryRequest& request, NavQueryPlan& plan)
{
    if (request.Zone.IsValid() && request.Zone != zone.Zone())
        return NavStatus::CrossZoneUnsupported;
    const std::optional<std::uint16_t> profile = zone.FindProfile(request.Profile);
    if (!profile)
        return NavStatus::ProfileUnavailable;

    plan = NavQueryPlan{};
    plan.Zone = &zone.GetBackend();
    plan.ProfileIndex = *profile;
    plan.Profile = &plan.Zone->Profiles[*profile];
    plan.Mesh = plan.Profile->Mesh.GetBackend().Mesh;
    plan.Request = &request;
    if (context.Query == nullptr
        || dtStatusFailed(context.Query->init(plan.Mesh, static_cast<int>(context.Config.MaxNodes))))
        return NavStatus::SearchLimitReached;

    CompileAreas(plan);
    CompileTraversalKinds(plan);
    context.Filter.Forbidden = plan.AreaForbidden;
    return NavStatus::Success;
}
