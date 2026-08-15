#include <net/NetDrivenSubject.h>

#include <ecs/World.h>
#include <net/NetReplicationComponents.h>

EntityId NetDrivenSubjectForPeer(const World& world, PeerId peer)
{
    if (!peer.IsValid() || !world.IsRegistered<NetDrivenBy>())
        return EntityId{};

    EntityId found;
    world.ForEachComponent<NetDrivenBy>(
        [&](EntityId entity, const NetDrivenBy& driven) {
            if (driven.Peer != peer.Value)
                return;
            if (!found.IsValid() || entity.Index < found.Index)
                found = entity;
        });
    return found;
}
