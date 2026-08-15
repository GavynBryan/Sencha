#include <net/NetZoneStreaming.h>

#include <ecs/World.h>
#include <net/NetOwnership.h>
#include <net/NetReplicationComponents.h>
#include <net/NetStats.h>
#include <physics/components/CharacterController.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldPartitionRuntime.h>

#include <algorithm>

namespace
{
    // Clear of kPrimaryFocusSource, which is what a locally driven pawn uses,
    // and of anything a game hands out itself.
    constexpr std::uint32_t kPeerSourceBit = 0x8000'0000u;

    bool ZoneLess(ZoneId a, ZoneId b) { return a.Value < b.Value; }
    bool SourceLess(FocusSourceId a, FocusSourceId b) { return a.Value < b.Value; }
}

FocusSourceId NetZoneStreaming::SourceFor(EntityId entity)
{
    return FocusSourceId{ kPeerSourceBit | entity.Index };
}

void NetZoneStreaming::Update(const World& world, EntityId localControlSubject,
                              NetSession* session,
                              ReplicationRuntime& replication,
                              WorldPartitionRuntime& partition, NetStats* traffic)
{
    if (!partition.HasManifest())
        return;

    FollowLocalPlayer(world, localControlSubject, partition);

    const NetSessionRole role =
        session == nullptr ? NetSessionRole::Standalone : session->Role();

    // A host streams around its peers as well as itself. A client does not:
    // NetOwner replicates, so walking it there would load the ground under
    // every other player -- silent waste rather than a visible failure.
    FollowPeers(role == NetSessionRole::Host ? &world : nullptr, partition);
    if (role == NetSessionRole::Host)
        OfferInterest(*session, replication, partition, traffic);

    if (role == NetSessionRole::Client)
        LoadWhatWasGranted(replication.LocalZones(), partition);
    else
        LoadWhatWasGranted(NetZoneScope{}, partition);
}

void NetZoneStreaming::FollowLocalPlayer(const World& world,
                                         EntityId localControlSubject,
                                         WorldPartitionRuntime& partition)
{
    const EntityId pawn = localControlSubject.IsValid()
        && world.IsAlive(localControlSubject)
        ? localControlSubject
        : EntityId{};
    if (!pawn.IsValid())
        return;

    if (const WorldTransform* transform = world.TryGet<WorldTransform>(pawn))
        partition.SetFocus(transform->Value.Position);
    if (world.IsRegistered<CharacterController>())
    {
        if (const CharacterController* shape =
                world.TryGet<CharacterController>(pawn))
        {
            partition.SetFocusCapsule(shape->Radius, shape->Height);
        }
    }
}

// `world` is null on a machine that is not the authority, which releases every
// source this held rather than leaving a former host streaming around peers it
// no longer serves.
void NetZoneStreaming::FollowPeers(const World* world,
                                   WorldPartitionRuntime& partition)
{
    Live_.clear();
    Claims_.clear();
    InterestZones_.clear();
    Interest_.clear();

    if (world != nullptr && world->IsRegistered<NetOwner>())
    {
        const bool hasCapsules = world->IsRegistered<CharacterController>();
        world->ForEachComponent<NetOwner>(
            [&](EntityId entity, const NetOwner& owner)
            {
                // Peer zero is the authority itself. What it owns it is only
                // simulating, and it is already streaming around whatever it
                // locally drives.
                if (owner.Peer == kNetAuthorityPeer)
                    return;
                const WorldTransform* transform =
                    world->TryGet<WorldTransform>(entity);
                if (transform == nullptr)
                    return;

                const FocusSourceId source = SourceFor(entity);
                partition.SetFocus(source, transform->Value.Position);
                if (hasCapsules)
                {
                    if (const CharacterController* shape =
                            world->TryGet<CharacterController>(entity))
                    {
                        partition.SetFocusCapsule(source, shape->Radius,
                                                  shape->Height);
                    }
                }
                Live_.push_back(source);

                // Claimed per entity and sorted into per-peer runs afterwards:
                // the walk visits a chunk at a time, so one peer's entities
                // arrive interleaved with another's and a run built as it goes
                // would not be contiguous.
                partition.DemandForSource(source, SourceZones_);
                for (const ZoneId zone : SourceZones_)
                    Claims_.push_back(Claim{ .Peer = owner.Peer, .Zone = zone });
            });
        std::sort(Live_.begin(), Live_.end(), SourceLess);
    }

    // Anything held a moment ago and not refreshed now is an entity nobody
    // drives any more -- destroyed, handed back, or belonging to a peer that
    // left. Releasing it tears nothing down: what it alone was holding enters
    // linger like any room somebody walked away from.
    for (const FocusSourceId held : Held_)
    {
        if (!std::binary_search(Live_.begin(), Live_.end(), held, SourceLess))
            partition.RemoveFocusSource(held);
    }
    Held_.swap(Live_);

    // Peer, then zone, so each peer's rooms come out ascending and contiguous,
    // which is what PublishZoneScope requires and what makes the result
    // independent of the order chunks happened to be visited in.
    std::sort(Claims_.begin(), Claims_.end(),
              [](const Claim& a, const Claim& b)
              {
                  if (a.Peer != b.Peer)
                      return a.Peer < b.Peer;
                  return a.Zone.Value < b.Zone.Value;
              });
    Claims_.erase(std::unique(Claims_.begin(), Claims_.end(),
                              [](const Claim& a, const Claim& b) {
                                  return a.Peer == b.Peer && a.Zone == b.Zone;
                              }),
                  Claims_.end());

    InterestZones_.reserve(Claims_.size());
    for (const Claim& claim : Claims_)
        InterestZones_.push_back(claim.Zone);

    // Built once the flat storage has stopped growing: a span taken while it
    // was still being appended to would not survive the next reallocation.
    std::size_t begin = 0;
    while (begin < Claims_.size())
    {
        std::size_t end = begin;
        while (end < Claims_.size() && Claims_[end].Peer == Claims_[begin].Peer)
            ++end;
        Interest_.push_back(NetPeerZoneInterest{
            .Peer = PeerId{ Claims_[begin].Peer },
            .Zones = std::span<const ZoneId>(InterestZones_)
                         .subspan(begin, end - begin),
        });
        begin = end;
    }
}

void NetZoneStreaming::OfferInterest(NetSession& session,
                                     ReplicationRuntime& replication,
                                     WorldPartitionRuntime& partition,
                                     NetStats* traffic)
{
    // Scope control applies only to rooms this world's manifest names. A zone
    // the streaming policy does not know is one no interest set can ever ask
    // for, and gating it would withhold it from everybody with no message that
    // could undo it.
    Streamed_.clear();
    for (const ZoneHeader& zone : partition.Manifest().Zones)
        Streamed_.push_back(zone.Id);
    replication.SetStreamedZones(Streamed_);

    const ReplicationRuntime::ZoneScopeStats scope =
        replication.PublishZoneScope(session, Interest_);
    if (traffic != nullptr && scope.BytesQueued > 0)
    {
        traffic->RecordOut(NetTrafficKind::Zone, scope.BytesQueued,
                           scope.Grants + scope.Revokes);
    }
}

void NetZoneStreaming::LoadWhatWasGranted(const NetZoneScope& scope,
                                          WorldPartitionRuntime& partition)
{
    Wanted_.clear();
    for (const NetZoneScope::Entry& held : scope.Entries())
        Wanted_.push_back(held.Zone);

    // Granted as well as acked: the grant is what tells this machine to start
    // loading, and waiting for the ack would be waiting for the load this is
    // meant to cause.
    for (const ZoneId zone : Wanted_)
    {
        if (!std::binary_search(Pinned_.begin(), Pinned_.end(), zone, ZoneLess))
        {
            partition.PinZone(zone, ZoneParticipation{ .Visible = true,
                                                       .Physics = true,
                                                       .Logic = true,
                                                       .Audio = true });
        }
    }
    // And the pin comes back off, or a client holds every room it ever visited
    // for the rest of the session.
    for (const ZoneId zone : Pinned_)
    {
        if (!std::binary_search(Wanted_.begin(), Wanted_.end(), zone, ZoneLess))
            partition.UnpinZone(zone);
    }
    Pinned_.swap(Wanted_);
}
