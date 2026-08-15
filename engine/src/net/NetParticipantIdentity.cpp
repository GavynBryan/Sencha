#include <net/NetParticipantIdentity.h>

#include <ecs/World.h>

EntityId NetParticipantForPeer(const World& world, PeerId peer)
{
    if (!peer.IsValid() || !world.IsRegistered<NetParticipantIdentity>())
        return EntityId{};

    EntityId found;
    world.ForEachComponent<NetParticipantIdentity>(
        [&](EntityId participant, const NetParticipantIdentity& identity) {
            if (identity.Peer == peer.Value
                && (!found.IsValid() || participant.Index < found.Index))
            {
                found = participant;
            }
        });
    return found;
}
