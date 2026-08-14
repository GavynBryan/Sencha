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

    // Whose input reaches it is deliberately not installed here. Ownership says
    // who a thing belongs to; NetPossess says who is at its controls. They agree
    // for a player's own pawn and part company for a vehicle somebody drives and
    // nobody owns, and it is their difference that keeps a departing driver from
    // taking the vehicle with them.
}

InputActionSourceId NetSourceForPeer(World& world, PeerId peer)
{
    if (!peer.IsValid())
        return kLocalInputActionSource;

    NetPeerSources& mapping = world.HasResource<NetPeerSources>()
        ? world.GetResource<NetPeerSources>()
        : world.AddResource<NetPeerSources>();

    const auto it = mapping.Sources.find(peer.Value);
    if (it != mapping.Sources.end())
        return it->second;

    InputActionSourceIds& ids = world.HasResource<InputActionSourceIds>()
        ? world.GetResource<InputActionSourceIds>()
        : world.AddResource<InputActionSourceIds>();

    const InputActionSourceId allocated = ids.Allocate();
    mapping.Sources.emplace(peer.Value, allocated);
    return allocated;
}

InputActionSourceId NetFindSourceForPeer(const World& world, PeerId peer)
{
    if (!peer.IsValid())
        return kLocalInputActionSource;

    const NetPeerSources* mapping = world.TryGetResource<NetPeerSources>();
    if (mapping == nullptr)
        return kLocalInputActionSource;

    const auto it = mapping->Sources.find(peer.Value);
    return it == mapping->Sources.end() ? kLocalInputActionSource : it->second;
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

    // The slot its commands were landing in. Nothing else closes one, so a
    // session that admits and drops peers all evening accumulates an action
    // state per peer until the process exits.
    const InputActionSourceId source = NetFindSourceForPeer(world, peer);
    if (source != kLocalInputActionSource)
    {
        if (InputActionSourceTable* sources =
                world.TryGetResource<InputActionSourceTable>())
        {
            sources->Close(source);
        }
    }

    // And the mapping itself. A session hands peer ids out in order and a new
    // session starts over, so a peer number outlives the peer that held it;
    // leaving the entry would hand the next holder the departed one's source,
    // and with it whatever input was still sitting in that slot.
    if (NetPeerSources* mapping = world.TryGetResource<NetPeerSources>())
        mapping->Sources.erase(peer.Value);
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
    if (!self.IsValid() || !world.IsRegistered<NetDrivenBy>())
        return;

    // What this peer drives, which is not the same question as what it owns. A
    // player at a turret owns their body and the turret both; only one of them
    // is where their input goes, and asking ownership could not tell which.
    //
    // Lowest-numbered of any duplicates. NetPossess gives a player one subject
    // at a time, so two is a defect rather than a shape -- deterministic here is
    // what makes such a defect reportable instead of intermittent.
    EntityId mine;
    world.ForEachComponent<NetDrivenBy>([&](EntityId entity, const NetDrivenBy& driven) {
        if (driven.Peer != self.Value)
            return;
        if (!mine.IsValid() || entity.Index < mine.Index)
            mine = entity;
    });

    if (mine == LocalControlSubjectOf(world))
        return;

    NetSetLocalControl(world, mine, &prediction);
}
