#pragma once

#include <core/identity/Id.h>
#include <zone/WorldConnectionComponents.h>
#include <zone/WorldPartitionIds.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

// Which links name each aperture. An aperture belongs to at most one link, so
// one named by two is contested for both, whatever their residency.
class ApertureClaims
{
public:
    void Add(const WorldLink& link)
    {
        for (const PersistableRef* end : { &link.EndpointA, &link.EndpointB })
        {
            if (!end->Target.IsValid())
                continue;
            std::vector<LinkId>& claimants = Claims_[end->Target.Value];
            if (std::find(claimants.begin(), claimants.end(), link.Id) == claimants.end())
                claimants.push_back(link.Id);
        }
    }

    [[nodiscard]] std::span<const LinkId> Claimants(PersistentEntityId aperture) const
    {
        const auto found = Claims_.find(aperture.Value);
        return found != Claims_.end() ? std::span<const LinkId>(found->second) : std::span<const LinkId>{};
    }

    [[nodiscard]] bool Contested(PersistentEntityId aperture) const { return Claimants(aperture).size() > 1; }

private:
    std::unordered_map<std::uint64_t, std::vector<LinkId>> Claims_;
};
