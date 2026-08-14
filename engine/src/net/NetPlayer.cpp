#include <net/NetPlayer.h>

#include <ecs/World.h>
#include <net/NetOwnership.h>

#include <vector>

namespace
{
    // Players live on components the authority registers. A world that never
    // registered them is one that never hosts, where every question here has no
    // subject.
    bool TracksPlayers(const World& world)
    {
        return world.IsRegistered<NetPlayer>()
            && world.IsRegistered<NetPlayerControl>();
    }

    // Takes this player's input off whatever it was reaching. Both halves come
    // off together: the reference deciding whose keys move it, and the record a
    // client reads to know it is the one driving.
    void ReleaseSubject(World& world, EntityId subject)
    {
        if (!subject.IsValid() || !world.IsAlive(subject))
            return;

        if (world.IsRegistered<InputActionSourceRef>()
            && world.HasComponent<InputActionSourceRef>(subject))
        {
            world.RemoveComponent<InputActionSourceRef>(subject);
        }

        // Removed rather than written back to the authority, unlike NetOwner.
        // Nothing reads this every tick to decide what it may be shown, so an
        // absence is a usable way to say nobody is at the controls -- and it
        // keeps an unoccupied turret from costing a field in every snapshot.
        if (world.IsRegistered<NetDrivenBy>()
            && world.HasComponent<NetDrivenBy>(subject))
        {
            world.RemoveComponent<NetDrivenBy>(subject);
        }
    }
}

EntityId NetPlayerForPeer(const World& world, PeerId peer)
{
    if (!TracksPlayers(world))
        return EntityId{};

    const std::uint32_t wanted = peer.IsValid() ? peer.Value : kNetAuthorityPeer;

    EntityId found;
    world.ForEachComponent<NetPlayer>([&](EntityId entity, const NetPlayer& player) {
        if (player.Peer == wanted && !found.IsValid())
            found = entity;
    });
    return found;
}

EntityId NetAdmitPlayer(World& world, PeerId peer, std::uint16_t partition)
{
    if (!TracksPlayers(world))
        return EntityId{};

    // Admitting the same peer twice is the same participant, not a second one.
    if (const EntityId existing = NetPlayerForPeer(world, peer); existing.IsValid())
        return existing;

    const EntityId player = world.CreateEntity(StoragePartitionId{ partition });
    world.AddComponent<NetPlayer>(
        player,
        NetPlayer{ .Peer = peer.IsValid() ? peer.Value : kNetAuthorityPeer });

    NetPlayerControl control;
    // A participant with no peer reads this machine's own devices; one with a
    // peer reads the slot its commands land in.
    control.Source = peer.IsValid() ? NetSourceForPeer(world, peer)
                                    : kLocalInputActionSource;
    world.AddComponent<NetPlayerControl>(player, control);

    // Players travel. A client that could not see the other participants could
    // not name them on a scoreboard, and could not find its own.
    if (world.IsRegistered<NetReplicated>()
        && !world.HasComponent<NetReplicated>(player))
    {
        world.AddComponent<NetReplicated>(player);
    }

    return player;
}

void NetPossess(World& world, EntityId player, EntityId subject)
{
    if (!TracksPlayers(world) || !world.IsAlive(player))
        return;

    NetPlayerControl* control = world.TryGet<NetPlayerControl>(player);
    if (control == nullptr)
        return;
    if (control->ControlSubject == subject)
        return;

    // The previous subject is released first, so no moment exists in which one
    // player's input reaches two entities.
    ReleaseSubject(world, control->ControlSubject);
    control->ControlSubject = EntityId{};

    if (!subject.IsValid() || !world.IsAlive(subject))
        return;

    // Taken from whoever else was recorded as driving it. A subject has one
    // driver: the entity itself can only name one source, so a second player
    // still holding it is a stale answer -- and stale is what decides wrongly
    // when that player is later retired and releases what it thinks it has.
    //
    // A walk of the player column, which costs the number of participants.
    std::vector<EntityId> displaced;
    const World& reading = world;
    reading.ForEachComponent<NetPlayerControl>(
        [&](EntityId other, const NetPlayerControl& held) {
            if (other != player && held.ControlSubject == subject)
                displaced.push_back(other);
        });
    for (const EntityId other : displaced)
    {
        if (NetPlayerControl* held = world.TryGet<NetPlayerControl>(other))
            held->ControlSubject = EntityId{};
    }

    // Re-fetched: the walk above wrote through other players' components, and
    // this one's pointer was taken before it.
    control = world.TryGet<NetPlayerControl>(player);
    if (control == nullptr)
        return;

    if (world.IsRegistered<InputActionSourceRef>())
    {
        if (InputActionSourceRef* ref = world.TryGet<InputActionSourceRef>(subject))
            ref->Source = control->Source;
        else
            world.AddComponent<InputActionSourceRef>(
                subject, InputActionSourceRef{ .Source = control->Source });
    }

    const NetPlayer* identity = world.TryGet<NetPlayer>(player);
    if (identity != nullptr && identity->Peer != kNetAuthorityPeer
        && world.IsRegistered<NetDrivenBy>())
    {
        if (NetDrivenBy* driven = world.TryGet<NetDrivenBy>(subject))
            driven->Peer = identity->Peer;
        else
            world.AddComponent<NetDrivenBy>(
                subject, NetDrivenBy{ .Peer = identity->Peer });
    }

    control->ControlSubject = subject;
}

void NetRetirePlayer(World& world, EntityId player)
{
    if (!TracksPlayers(world) || !world.IsAlive(player))
        return;

    if (NetPlayerControl* control = world.TryGet<NetPlayerControl>(player))
    {
        // What they were driving is let go of, never destroyed. A player who
        // disconnects at the controls of a turret does not own the turret, and
        // the body they left behind is the game's to decide about.
        ReleaseSubject(world, control->ControlSubject);
        control->ControlSubject = EntityId{};
    }

    world.DestroyEntity(player);
}
