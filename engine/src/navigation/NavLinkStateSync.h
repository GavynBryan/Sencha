#pragma once

#include <ecs/Query.h>
#include <navigation/NavLinkState.h>
#include <navigation/NavigationIds.h>
#include <zone/ZoneId.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

class RuntimeWorld;
class ZoneNavigation;

// Knows which resident zone holds each navigation link, and applies gameplay
// NavLinkState components to their links. Several components naming one link
// combine order-independently: enabled only if all enable it, the largest cost
// scale wins. A link no component names returns to its authored state.
class NavLinkStateSync
{
public:
    struct ApplyResult
    {
        std::size_t StateComponents = 0;
        std::size_t UnknownLinks = 0;
        std::size_t LinksChanged = 0;
    };

    void IndexZone(ZoneId zone, const ZoneNavigation& navigation);
    void ForgetZone(ZoneId zone);
    [[nodiscard]] std::optional<ZoneId> FindLinkZone(NavLinkId link) const;

    ApplyResult Apply(RuntimeWorld& runtime);

private:
    struct LinkZone
    {
        NavLinkId Link;
        ZoneId Zone;
    };

    // What the components naming one link ask for, combined.
    struct RequestedState
    {
        bool Named = false;
        bool Enabled = true;
        float CostScale = 1.0f;

        void Combine(const NavLinkState& state);
    };

    [[nodiscard]] std::size_t IndexOf(NavLinkId link) const;
    void CollectRequests(RuntimeWorld& runtime, ApplyResult& result);

    // Sorted by (link, zone).
    std::vector<LinkZone> LinkZones;
    // Parallel to LinkZones; rebuilt each Apply.
    std::vector<RequestedState> Requested;
    std::unique_ptr<Query<Read<NavLinkState>>> States;
};
