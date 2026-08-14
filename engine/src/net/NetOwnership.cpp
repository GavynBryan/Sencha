#include <net/NetOwnership.h>

#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <net/ClientPrediction.h>
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

    // The second half of the same fact. NetOwner decides whose aim turns the
    // entity; this decides whose keys move it, and the two disagreeing is the
    // shape of the defect where a host steers a pawn it is only simulating on
    // someone else's behalf.
    if (!world.IsRegistered<InputActionSourceRef>())
        return;
    if (InputActionSourceRef* source = world.TryGet<InputActionSourceRef>(entity))
        source->Source = peer.Value;
    else
        world.AddComponent<InputActionSourceRef>(
            entity, InputActionSourceRef{ .Source = peer.Value });
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

    // The reference is not replicated and means "somebody else's keys", so
    // absence is the right way to say there is nobody.
    if (world.IsRegistered<InputActionSourceRef>()
        && world.HasComponent<InputActionSourceRef>(entity))
    {
        world.RemoveComponent<InputActionSourceRef>(entity);
    }
}

void NetForgetOwnerPeer(World& world, PeerId peer)
{
    if (!peer.IsValid() || !TracksOwnership(world))
        return;

    std::vector<EntityId> owned;
    NetOwnedBy(world, peer, owned);
    for (const EntityId entity : owned)
        NetClearOwner(world, entity);

    // The slot its commands were landing in. Nothing else closes one, so a
    // session that admits and drops peers all evening accumulates an action
    // state per peer until the process exits.
    if (InputActionSourceTable* sources =
            world.TryGetResource<InputActionSourceTable>())
    {
        sources->Close(peer.Value);
    }
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

//=============================================================================
// Local control
//=============================================================================

EntityId LocalControlSubjectOf(const World& world)
{
    const LocalControlSubject* held = world.TryGetResource<LocalControlSubject>();
    if (held == nullptr || !held->Value.IsValid() || !world.IsAlive(held->Value))
        return EntityId{};
    return held->Value;
}

void NetSetLocalControl(World& world, EntityId entity, ClientPrediction* prediction)
{
    const EntityId previous = LocalControlSubjectOf(world);
    if (previous == entity)
        return;

    // The previous holder is released first. Leaving the tag behind leaves a
    // second entity turning with the player's mouse, and only one of them has
    // the camera on it.
    if (previous.IsValid() && world.IsRegistered<LocalLookControl>()
        && world.HasComponent<LocalLookControl>(previous))
    {
        world.RemoveComponent<LocalLookControl>(previous);
    }

    if (entity.IsValid() && world.IsRegistered<LocalLookControl>()
        && !world.HasComponent<LocalLookControl>(entity))
    {
        world.AddComponent<LocalLookControl>(entity, {});
    }

    if (LocalControlSubject* held = world.TryGetResource<LocalControlSubject>())
        held->Value = entity;
    else
        world.AddResource<LocalControlSubject>().Value = entity;

    // Prediction eligibility is ownership seen from the machine that has it: the
    // one entity this client simulates ahead is the one it drives. An invalid
    // subject clears it, which is what nothing did before -- a client that lost
    // its pawn went on predicting it.
    if (prediction != nullptr)
        prediction->SetPredicted(entity);
}

void NetReconcileLocalControl(World& world, PeerId self, ClientPrediction& prediction)
{
    if (!self.IsValid() || !TracksOwnership(world))
        return;

    // The lowest-numbered entity this peer owns, so two of them is a stable
    // answer rather than one that changes with hash order. Owning two is not a
    // supported shape yet; picking deterministically is what makes it a
    // reportable bug instead of an intermittent one.
    EntityId mine;
    world.ForEachComponent<NetOwner>([&](EntityId entity, const NetOwner& owner) {
        if (owner.Peer != self.Value)
            return;
        if (!mine.IsValid() || entity.Index < mine.Index)
            mine = entity;
    });

    if (mine == LocalControlSubjectOf(world))
        return;

    NetSetLocalControl(world, mine, &prediction);
}
