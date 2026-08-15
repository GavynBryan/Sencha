#include <net/NetOwnership.h>

#include <ecs/World.h>
#include <net/NetReplicationComponents.h>

namespace
{
    // Ownership is the authority's record and lives on components the authority
    // registers. A world that never registered them is one that never hosts,
    // where every call here is a question with no subject.
    bool TracksOwnership(const World& world)
    {
        return world.IsRegistered<NetOwner>();
    }
}

void NetSetOwner(World& world, EntityId entity, PeerId peer)
{
    if (!world.IsAlive(entity) || !TracksOwnership(world))
        return;
    if (!peer.IsValid())
    {
        NetClearOwner(world, entity);
        return;
    }

    if (NetOwner* owner = world.TryGet<NetOwner>(entity))
        owner->Peer = peer.Value;
    else
        world.AddComponent<NetOwner>(entity, NetOwner{ .Peer = peer.Value });

    // Whose input reaches it is deliberately not installed here. Ownership and
    // participant control agree for a player's own pawn and part company for a
    // vehicle somebody drives and nobody owns.
}

void NetClearOwner(World& world, EntityId entity)
{
    if (!world.IsAlive(entity) || !TracksOwnership(world))
        return;

    // Written rather than removed. A snapshot carries values and has no way to
    // say a component is gone for a field a client reads every tick, so the
    // authority owning something is a number and not an absence.
    if (NetOwner* owner = world.TryGet<NetOwner>(entity))
        owner->Peer = kNetAuthorityPeer;

    // Whoever is at the controls stays there. Handing a vehicle back to the
    // authority does not tip its driver out of the seat, and a caller that means
    // both says both.
}

void NetForgetOwnerPeer(World& world, PeerId peer)
{
    if (!peer.IsValid() || !TracksOwnership(world))
        return;

    std::vector<EntityId> owned;
    NetOwnedBy(world, peer, owned);
    for (const EntityId entity : owned)
        NetClearOwner(world, entity);
}

PeerId NetOwnerOf(const World& world, EntityId entity)
{
    if (!world.IsAlive(entity) || !TracksOwnership(world))
        return PeerId{};
    const NetOwner* owner = world.TryGet<NetOwner>(entity);
    return owner == nullptr ? PeerId{} : PeerId{ owner->Peer };
}

void NetOwnedBy(const World& world, PeerId peer, std::vector<EntityId>& out)
{
    out.clear();
    if (!peer.IsValid() || !TracksOwnership(world))
        return;

    world.ForEachComponent<NetOwner>([&](EntityId entity, const NetOwner& owner) {
        if (owner.Peer == peer.Value)
            out.push_back(entity);
    });
}
