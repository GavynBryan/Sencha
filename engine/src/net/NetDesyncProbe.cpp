#include <net/NetDesyncProbe.h>

#include <ecs/World.h>
#include <net/NetProtocol.h>
#include <net/NetReplicationComponents.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationCodec.h>
#include <net/ReplicationInterpolation.h>
#include <world/transform/TransformComponents.h>

#include <cstring>

namespace
{
    // FNV-1a's offset basis, so an empty fold is a defined value rather than
    // zero -- which is also what a peer sends when it has nothing to say.
    constexpr std::uint64_t kSeed = 14695981039346656037ull;

    // Whether every field run of every component this entity carries was
    // already confirmed at that peer's floor. Anything less and the two sides
    // are correct to differ: what a peer has not been sent yet is not a
    // disagreement about the world, it is a snapshot that has not arrived.
    bool FullyProved(const ReplicationChangeStore::EntityState& entity,
                     std::uint64_t floor)
    {
        if (floor == 0)
            return false;
        // A removal it has not been told about is state it still holds and the
        // authority does not, which would read as divergence and is really a
        // message in flight.
        for (const ReplicationChangeStore::RemovedComponent& gone : entity.Removed)
        {
            if (gone.RemovedAt > floor)
                return false;
        }
        for (const ReplicationChangeStore::ComponentState& held : entity.Components)
        {
            for (const std::uint64_t changedAt : held.ChangedAt)
            {
                if (changedAt > floor)
                    return false;
            }
        }
        return true;
    }
}

std::size_t NetEncodeDesyncReport(std::uint64_t tick,
                                  std::span<const NetDesyncSample> samples,
                                  std::span<std::byte> out)
{
    if (samples.size() > kNetMaxDesyncSamples)
        return 0;

    NetWriter writer(out);
    writer.WriteU8(static_cast<std::uint8_t>(NetPayloadKind::DesyncHash));
    writer.WriteU64(tick);
    writer.WriteU8(static_cast<std::uint8_t>(samples.size()));
    for (const NetDesyncSample& sample : samples)
    {
        writer.WriteU64(sample.Id.Value);
        writer.WriteU64(sample.Hash);
    }
    return writer.Overflowed() ? 0 : writer.Size();
}

bool NetDecodeDesyncReport(std::span<const std::byte> bytes, std::uint64_t& tick,
                           std::vector<NetDesyncSample>& samples)
{
    samples.clear();
    if (bytes.size() < kNetPayloadKindBytes)
        return false;
    if (static_cast<NetPayloadKind>(bytes[0]) != NetPayloadKind::DesyncHash)
        return false;

    NetReader reader(bytes.subspan(kNetPayloadKindBytes));
    if (!reader.ReadU64(tick))
        return false;

    std::uint8_t count = 0;
    if (!reader.ReadU8(count) || count > kNetMaxDesyncSamples)
        return false;

    samples.reserve(count);
    for (std::uint8_t i = 0; i < count; ++i)
    {
        std::uint64_t id = 0;
        std::uint64_t hash = 0;
        if (!reader.ReadU64(id) || !reader.ReadU64(hash) || id == 0)
            return false;
        samples.push_back(NetDesyncSample{ NetEntityId{ id }, hash });
    }
    return reader.AtEnd();
}

void NetBuildDesyncReport(const ReplicationChangeStore& changes,
                          const ReplicationLayout& layout,
                          const ReplicationPeerState& peer,
                          std::uint32_t ownerPeer, std::size_t& cursor,
                          std::vector<NetDesyncSample>& samples)
{
    samples.clear();
    const std::span<const ReplicationChangeStore::EntityState> live = changes.Live();
    if (live.empty())
    {
        cursor = 0;
        return;
    }

    if (cursor >= live.size())
        cursor = 0;

    // One lap at most, so a world with nothing eligible costs one pass rather
    // than spinning. Where it stops is where the next report starts, which is
    // how coverage reaches a world larger than one report.
    const std::size_t start = cursor;
    for (std::size_t step = 0; step < live.size(); ++step)
    {
        const std::size_t at = (start + step) % live.size();
        const ReplicationChangeStore::EntityState& entity = live[at];
        cursor = (at + 1) % live.size();

        const std::uint64_t floor = peer.Floor(entity.Id);
        if (!FullyProved(entity, floor))
            continue;

        const bool isOwner = ownerPeer != 0 && entity.Owner == ownerPeer;
        std::uint64_t hash = kSeed;
        for (const ReplicationChangeStore::ComponentState& held : entity.Components)
        {
            const ReplicatedComponent* component = layout.At(held.WireIndex);
            if (component == nullptr)
                continue;
            // Predicted state is skipped only on the entity this peer drives.
            // Elsewhere a predicted component is ordinary replicated state that
            // lands in the world like any other -- and since the transform is
            // predicted, skipping it everywhere would leave the probe comparing
            // almost nothing.
            if (component->Predicted && isOwner)
                continue;
            hash = ReplicationFoldFields(
                hash, *component, held.Bytes,
                ReplicationVisibleFields(*component, isOwner));
        }

        samples.push_back(NetDesyncSample{ entity.Id, hash });
        if (samples.size() >= kNetMaxDesyncSamples)
            return;
    }
}

bool NetFoldLocalEntity(const World& world, const ReplicationLayout& layout,
                        const ReplicationClientIdentity& identity,
                        const ReplicationInterpolation* interpolation,
                        std::uint32_t selfPeer, NetEntityId id,
                        std::uint64_t& hash)
{
    const EntityId entity = identity.TryResolve(id);
    if (!entity.IsValid() || !world.IsAlive(entity))
        return false;

    bool isOwner = false;
    if (world.IsRegistered<NetOwner>())
    {
        if (const NetOwner* owner = world.TryGet<NetOwner>(entity))
            isOwner = selfPeer != 0 && owner->Peer == selfPeer;
    }

    hash = kSeed;
    // Walked in layout order rather than in storage order, because the two sides
    // have to fold the same fields in the same sequence and only the layout is
    // the same on both.
    for (std::size_t index = 0; index < layout.Size(); ++index)
    {
        const ReplicatedComponent* component =
            layout.At(static_cast<std::uint8_t>(index));
        if (component == nullptr)
            continue;
        if (component->Predicted && isOwner)
            continue;
        if (!world.IsRegistered(component->Type))
            continue;
        const ComponentId column = world.GetComponentIdByType(component->Type);
        if (!world.HasComponent(entity, column))
            continue;
        const void* raw = world.GetComponentRaw(entity, column);
        if (raw == nullptr)
            continue;

        // The authority's last word on a pose, not the blend standing on the
        // entity. A mirrored transform is a presented value between two
        // authoritative ones, so folding it would report everything that moves
        // as divergent.
        const LocalTransform* authoritative = nullptr;
        if (interpolation != nullptr && interpolation->InterceptsPose(component->Type))
            authoritative = interpolation->TryAuthoritative(entity);

        const auto* bytes = authoritative != nullptr
                                ? reinterpret_cast<const std::byte*>(authoritative)
                                : static_cast<const std::byte*>(raw);
        hash = ReplicationFoldFields(
            hash, *component, std::span(bytes, component->Size),
            ReplicationVisibleFields(*component, isOwner));
    }
    return true;
}

NetDesyncResult NetCheckDesyncReport(
    const World& world, const ReplicationLayout& layout,
    const ReplicationClientIdentity& identity,
    const ReplicationInterpolation* interpolation, std::uint32_t selfPeer,
    std::span<const NetDesyncSample> samples)
{
    NetDesyncResult result;
    for (const NetDesyncSample& sample : samples)
    {
        std::uint64_t mine = 0;
        if (!NetFoldLocalEntity(world, layout, identity, interpolation, selfPeer,
                                sample.Id, mine))
        {
            ++result.Absent;
            continue;
        }
        ++result.Compared;
        if (mine == sample.Hash)
            continue;
        ++result.Diverged;
        if (!result.FirstDiverged.IsValid())
            result.FirstDiverged = sample.Id;
    }
    return result;
}
