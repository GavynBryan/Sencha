#include <net/NetOwnedFocus.h>

#include <ecs/World.h>
#include <net/NetReplicationComponents.h>
#include <physics/components/CharacterController.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldPartitionRuntime.h>

#include <algorithm>

FocusSourceId NetFocusSourceFor(EntityId entity)
{
    return FocusSourceId{ kNetFocusSourceBit | entity.Index };
}

void NetOwnedFocus::Update(const World& world, NetSessionRole role,
                           WorldPartitionRuntime& partition)
{
    Live_.clear();
    Claims_.clear();
    InterestZones_.clear();
    Interest_.clear();

    // A manifest is what makes a position mean a zone. Without one every call
    // below would mint a source carrying nothing, and the release pass would
    // then take them all away again next frame.
    if (role == NetSessionRole::Host && partition.HasManifest()
        && world.IsRegistered<NetOwner>())
    {
        const bool hasCapsules = world.IsRegistered<CharacterController>();

        world.ForEachComponent<NetOwner>(
            [&](EntityId entity, const NetOwner& owner)
            {
                // Peer zero is the authority itself. What it owns it is only
                // simulating, and it is already streaming around whatever it
                // locally drives.
                if (owner.Peer == kNetAuthorityPeer)
                    return;

                const WorldTransform* transform =
                    world.TryGet<WorldTransform>(entity);
                if (transform == nullptr)
                    return;

                const FocusSourceId source = NetFocusSourceFor(entity);
                partition.SetFocus(source, transform->Value.Position);
                if (hasCapsules)
                {
                    if (const CharacterController* shape =
                            world.TryGet<CharacterController>(entity))
                    {
                        partition.SetFocusCapsule(
                            source, shape->Radius, shape->Height);
                    }
                }
                Live_.push_back(source);

                // What this peer should be holding open. Claimed per entity and
                // sorted into per-peer runs afterwards rather than accumulated
                // in place: the walk visits a chunk at a time, so one peer's
                // entities arrive interleaved with another's and a run built as
                // it goes would not be contiguous.
                partition.DemandForSource(source, SourceZones_);
                for (const ZoneId zone : SourceZones_)
                    Claims_.push_back(Claim{ .Peer = owner.Peer, .Zone = zone });
            });

        std::sort(Live_.begin(), Live_.end(),
                  [](FocusSourceId a, FocusSourceId b)
                  { return a.Value < b.Value; });
    }

    // Anything held a moment ago and not refreshed now is an entity nobody
    // drives any more -- destroyed, handed back, or belonging to a peer that
    // left. Releasing it does not tear anything down; what it alone was holding
    // enters linger like any zone somebody walked away from.
    for (const FocusSourceId held : Held_)
    {
        const bool live = std::binary_search(
            Live_.begin(), Live_.end(), held,
            [](FocusSourceId a, FocusSourceId b) { return a.Value < b.Value; });
        if (!live)
            partition.RemoveFocusSource(held);
    }

    Held_.swap(Live_);

    // Peer, then zone, so each peer's zones come out ascending and contiguous
    // -- which is the shape PublishZoneScope requires, and the shape that makes
    // the result independent of the order chunks happened to be visited in.
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
            .Zones = std::span<const ZoneId>(InterestZones_).subspan(begin, end - begin),
        });
        begin = end;
    }
}
