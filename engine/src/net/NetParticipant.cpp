#include <net/NetParticipant.h>

#include <ecs/World.h>
#include <net/NetOwnership.h>
#include <net/NetPlayer.h>
#include <net/NetReplicationComponents.h>

EntityId NetAdmitParticipant(World& world, PeerId peer,
                             const NetParticipantPolicies& policies)
{
    const EntityId player = NetAdmitPlayer(world, peer);
    if (!player.IsValid())
        return player;

    if (policies.BuildPlayer)
        policies.BuildPlayer(world, player);

    // Asked once, here. A participant that gets no body now is not owed one
    // later by the engine -- that is the game's call, and the seam for it is
    // NetRequestPlayerBody.
    (void)NetRequestPlayerBody(world, player, policies);
    return player;
}

EntityId NetRequestPlayerBody(World& world, EntityId player,
                              const NetParticipantPolicies& policies)
{
    if (!world.IsAlive(player) || !policies.ProvideBody)
        return EntityId{};

    const NetPlayerControl* held = world.TryGet<NetPlayerControl>(player);
    if (held == nullptr)
        return EntityId{};
    if (held->Body.IsValid() && world.IsAlive(held->Body))
        return held->Body;

    const EntityId body = policies.ProvideBody(world, player);
    if (!body.IsValid() || !world.IsAlive(body))
        return EntityId{};

    // Everything that follows from a body being somebody's. Travelling, because
    // other machines have to see it; owned, because its owner-only state is
    // theirs; driven, because their keys reach it.
    if (world.IsRegistered<NetReplicated>()
        && !world.HasComponent<NetReplicated>(body))
    {
        world.AddComponent<NetReplicated>(body);
    }

    const NetPlayer* identity = world.TryGet<NetPlayer>(player);
    if (identity != nullptr && identity->Peer != kNetAuthorityPeer)
        NetSetOwner(world, body, PeerId{ identity->Peer });

    NetPossess(world, player, body);

    // Re-fetched rather than held across the calls above: all of them are
    // structural, and a component pointer does not survive structural change.
    if (NetPlayerControl* bound = world.TryGet<NetPlayerControl>(player))
        bound->Body = body;
    return body;
}

EntityId NetRetireParticipant(World& world, EntityId player,
                              const NetParticipantPolicies& policies)
{
    if (!player.IsValid() || !world.IsAlive(player))
        return EntityId{};

    EntityId body;
    if (const NetPlayerControl* held = world.TryGet<NetPlayerControl>(player))
        body = held->Body;

    // Read before the player is destroyed, and acted on after: the policy is
    // entitled to look at the player it is being asked about.
    bool reap = body.IsValid() && world.IsAlive(body);
    if (reap && policies.ReapBody)
        reap = policies.ReapBody(world, player, body);

    NetRetirePlayer(world, player);

    if (!reap || !world.IsAlive(body))
        return EntityId{};

    world.DestroyEntity(body);
    return body;
}
