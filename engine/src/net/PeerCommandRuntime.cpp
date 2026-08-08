#include <net/PeerCommandRuntime.h>

#include <app/EngineSchedule.h>
#include <controller/LookOrientation.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <net/NetReplicationComponents.h>
#include <net/PawnCommandCapture.h>

#include <algorithm>

namespace
{
    constexpr std::size_t kKindBytes = 1;
}

bool PeerCommandRuntime::Receive(PeerId peer, std::span<const std::byte> payload)
{
    if (payload.size() < kKindBytes)
        return false;
    if (static_cast<NetPayloadKind>(payload[0]) != NetPayloadKind::Command)
        return false;

    NetBitReader reader(payload.subspan(kKindBytes));
    NetPlayerCommand command;
    if (!NetDecodePlayerCommand(reader, command))
        return false;

    Buffers[peer].Receive(command);
    return true;
}

void PeerCommandRuntime::Feed(World& world, std::uint64_t tick)
{
    if (Buffers.empty())
        return;

    // Peer records are indexed by this machine's action vocabulary, because
    // both ends compiled the same one from the same content. Before a profile
    // binds there is no vocabulary to index by, and a record cannot mean
    // anything yet.
    const InputActionState* local = world.TryGetResource<InputActionState>();
    const std::size_t actionCount = local == nullptr ? 0 : local->ActionCount();
    if (actionCount == 0)
        return;

    InputActionSourceTable& sources = world.HasResource<InputActionSourceTable>()
        ? world.GetResource<InputActionSourceTable>()
        : world.AddResource<InputActionSourceTable>();

    // What each peer's tick came to, so the aim that framed a record is applied
    // with that record rather than with whichever datagram landed last.
    Consumed.clear();

    for (auto& [peer, buffer] : Buffers)
    {
        buffer.SetTargetDepth(Target);

        NetCommandRecord record;
        if (!buffer.Next(record))
            continue;
        Consumed.emplace(peer, record);

        // A peer's id is its input source id. Peer ids start at one and source
        // zero is this machine's own, so the two spaces cannot collide.
        InputActionState& state = sources.Open(peer.Value, actionCount);
        const std::span<InputActionValue> storage = state.BeginTick(tick);

        // Every slot is written, not just the ones the record filled: the ring
        // hands back whatever occupied this slot several ticks ago, and a
        // shorter record would otherwise leave that stale input live.
        const std::size_t copied =
            std::min<std::size_t>({ storage.size(), record.ActionCount });
        for (std::size_t index = 0; index < storage.size(); ++index)
        {
            storage[index] =
                index < copied ? record.Actions[index] : InputActionValue{};
        }
    }

    if (!world.IsRegistered<NetOwner>() || !world.IsRegistered<LookOrientation>())
        return;

    // Aim goes to the pawn the authority recorded this peer as owning, which is
    // the only thing that decides whose view a command turns. It comes from the
    // record this tick consumed, so a tick is simulated with the aim it was
    // taken with -- including a starved tick, whose repeated record carries the
    // last aim the player actually held.
    const World& reader = world;
    reader.ForEachComponent<NetOwner>([&](EntityId entity, const NetOwner& owner) {
        const auto it = Consumed.find(PeerId{ owner.Peer });
        if (it == Consumed.end())
            return;

        LookOrientation* look = world.TryGet<LookOrientation>(entity);
        if (look == nullptr)
            return;
        look->Yaw = it->second.Yaw;
        look->Pitch = std::clamp(it->second.Pitch, look->MinPitch, look->MaxPitch);
    });
}

std::size_t PeerCommandRuntime::SendLocal(NetSession& session,
                                          const PawnCommandRing& ring)
{
    if (session.Role() != NetSessionRole::Client || !session.IsConnected())
        return 0;
    if (ring.Size() == 0)
        return 0;

    // Straight off the ring: these are the ticks this machine simulated, under
    // the names it simulated them by, with the aim each was taken with. Nothing
    // is re-sampled here, because anything sampled at send time is a different
    // moment from the one the tick ran at.
    NetPlayerCommand command;
    const std::size_t window =
        std::min<std::size_t>(ring.Size(), kNetMaxCommandRecords);
    for (std::size_t index = 0; index < window; ++index)
    {
        const PawnCommandTick& source = ring.Recent(index);
        NetCommandRecord& record = command.Records[index];
        record.Tick = source.Tick;
        record.Yaw = source.Yaw;
        record.Pitch = source.Pitch;
        record.ActionCount = source.ActionCount;
        for (std::uint8_t action = 0; action < record.ActionCount; ++action)
            record.Actions[action] = source.Actions[action];
    }
    command.RecordCount = static_cast<std::uint8_t>(window);

    if (Scratch.size() < kNetMaxPayloadBytes)
        Scratch.resize(kNetMaxPayloadBytes);
    Scratch[0] = static_cast<std::byte>(NetPayloadKind::Command);

    NetBitWriter writer(std::span<std::byte>(Scratch).subspan(kKindBytes));
    if (NetEncodePlayerCommand(command, writer) == 0)
        return 0;

    const std::size_t bytes = kKindBytes + writer.BytesWritten();
    if (!session.Send(session.LocalPeerId(), NetChannelKind::UnreliableSequenced,
                      std::span<const std::byte>(Scratch).subspan(0, bytes)))
    {
        return 0;
    }
    return bytes;
}

void PeerCommandRuntime::ForgetPeer(PeerId peer)
{
    Buffers.erase(peer);
}

void PeerCommandRuntime::Reset()
{
    Buffers.clear();
}

std::uint64_t PeerCommandRuntime::AckFor(PeerId peer) const
{
    const auto it = Buffers.find(peer);
    return it == Buffers.end() ? 0 : it->second.ConsumedThrough();
}

const NetPeerCommandBuffer* PeerCommandRuntime::Peer(PeerId peer) const
{
    const auto it = Buffers.find(peer);
    return it == Buffers.end() ? nullptr : &it->second;
}

void PeerCommandFeedSystem::FixedLogic(FixedLogicContext& ctx)
{
    if (Commands == nullptr)
        return;
    Commands->Feed(ctx.Entities, ctx.Time.TickIndex);
}

void RegisterNetSystems(EngineSchedule& schedule, PeerCommandRuntime& commands,
                        ClientPrediction& prediction,
                        ReplicationInterpolation& interpolation,
                        const NetTickEstimator& clock)
{
    schedule.Register<PeerCommandFeedSystem>(commands);
    schedule.Register<ReplicationInterpolationSystem>(interpolation, prediction, clock);
    schedule.Register<PawnCommandCaptureSystem>(prediction, clock);
}
