#include "NavLinkStateSync.h"

#include <navigation/ZoneNavigation.h>
#include <world/RuntimeWorld.h>

#include <algorithm>
#include <tuple>

void NavLinkStateSync::RequestedState::Combine(const NavLinkState& state)
{
    Enabled = Enabled && state.Enabled;
    CostScale = Named ? std::max(CostScale, state.CostScale) : state.CostScale;
    Named = true;
}

void NavLinkStateSync::IndexZone(ZoneId zone, const ZoneNavigation& navigation)
{
    for (std::uint32_t i = 0; i < navigation.LinkCount(); ++i)
        LinkZones.push_back(LinkZone{ navigation.Link(i).Id, zone });
    std::ranges::sort(LinkZones, {}, [](const LinkZone& entry)
                      { return std::tuple(entry.Link.Value, entry.Zone.Value); });
}

void NavLinkStateSync::ForgetZone(ZoneId zone)
{
    std::erase_if(LinkZones, [zone](const LinkZone& entry) { return entry.Zone == zone; });
}

std::size_t NavLinkStateSync::IndexOf(NavLinkId link) const
{
    const auto found = std::ranges::lower_bound(LinkZones, link.Value, {},
                                                [](const LinkZone& entry) { return entry.Link.Value; });
    return found != LinkZones.end() && found->Link == link
        ? static_cast<std::size_t>(found - LinkZones.begin())
        : LinkZones.size();
}

std::optional<ZoneId> NavLinkStateSync::FindLinkZone(NavLinkId link) const
{
    const std::size_t index = IndexOf(link);
    return index < LinkZones.size() ? std::optional(LinkZones[index].Zone) : std::nullopt;
}

void NavLinkStateSync::CollectRequests(RuntimeWorld& runtime, ApplyResult& result)
{
    Requested.assign(LinkZones.size(), RequestedState{});
    World& world = runtime.Entities();
    if (!world.IsRegistered<NavLinkState>())
        return;
    if (States == nullptr)
        States = std::make_unique<Query<Read<NavLinkState>>>(world);
    States->ForEachChunk([&](auto& view)
    {
        for (const NavLinkState& state : view.template Read<NavLinkState>())
        {
            ++result.StateComponents;
            const std::size_t index = IndexOf(state.Link);
            if (index == LinkZones.size())
                ++result.UnknownLinks;
            else
                Requested[index].Combine(state);
        }
    });
}

NavLinkStateSync::ApplyResult NavLinkStateSync::Apply(RuntimeWorld& runtime)
{
    ApplyResult result;
    if (LinkZones.empty())
        return result;
    CollectRequests(runtime, result);
    for (std::size_t i = 0; i < LinkZones.size(); ++i)
    {
        ZoneNavigation* navigation = FindZoneNavigation(runtime, LinkZones[i].Zone);
        const std::optional<std::uint32_t> link =
            navigation != nullptr ? navigation->FindLink(LinkZones[i].Link) : std::nullopt;
        if (!link)
            continue;
        const std::uint32_t revision = navigation->Link(*link).Revision;
        navigation->SetLinkState(*link, Requested[i].Enabled, Requested[i].CostScale);
        if (navigation->Link(*link).Revision != revision)
            ++result.LinksChanged;
    }
    return result;
}
