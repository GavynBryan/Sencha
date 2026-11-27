#include <zone/GainedSpawnRequests.h>

#include <algorithm>
#include <utility>

void GainedSpawnRequests::Add(GainedSpawnRequest request)
{
    Remove(request.Scope, request.Id);
    Waiting.push_back(std::move(request));
}

void GainedSpawnRequests::Remove(ZoneStateScope scope, PersistentEntityId id)
{
    std::erase_if(Waiting, [&](const GainedSpawnRequest& request) { return request.Scope == scope && request.Id == id; });
}

void GainedSpawnRequests::ForgetZone(ZoneStateScope scope)
{
    std::erase_if(Waiting, [&](const GainedSpawnRequest& request) { return request.Scope == scope; });
}

bool GainedSpawnRequests::Awaits(ZoneStateScope scope, PersistentEntityId id) const
{
    return std::ranges::any_of(
        Waiting, [&](const GainedSpawnRequest& request) { return request.Scope == scope && request.Id == id; });
}

bool GainedSpawnRequests::AwaitsHolder(PersistentEntityId id) const
{
    return std::ranges::any_of(Waiting, [&](const GainedSpawnRequest& request) { return request.Id == id; });
}
