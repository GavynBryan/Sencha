#include <net/PeerCommandRuntime.h>

#include <app/EngineSchedule.h>
#include <controller/LookOrientation.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <net/NetReplicationComponents.h>

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

    for (auto& [peer, buffer] : Buffers)
    {
        buffer.SetTargetDepth(Target);

        NetCommandRecord record;
        if (!buffer.Next(record))
            continue;

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
    // the only thing that decides whose view a command turns.
    const World& reader = world;
    reader.ForEachComponent<NetOwner>([&](EntityId entity, const NetOwner& owner) {
        const auto it = Buffers.find(PeerId{ owner.Peer });
        if (it == Buffers.end() || !it->second.HasAim())
            return;

        LookOrientation* look = world.TryGet<LookOrientation>(entity);
        if (look == nullptr)
            return;
        look->Yaw = it->second.Yaw();
        look->Pitch = std::clamp(it->second.Pitch(), look->MinPitch, look->MaxPitch);
    });
}

std::size_t PeerCommandRuntime::SendLocal(NetSession& session, const World& world,
                                          std::int64_t tickOffset)
{
    if (session.Role() != NetSessionRole::Client || !session.IsConnected())
        return 0;

    const InputActionState* actions = world.TryGetResource<InputActionState>();
    if (actions == nullptr || actions->HistoryCount() == 0)
        return 0;
    if (!world.IsRegistered<LookOrientation>() || !world.IsRegistered<LocalLookControl>())
        return 0;

    NetPlayerCommand command;

    // Where this player is aiming, from the one pawn this process drives. Look
    // integrates locally on the presentation clock so the view never waits for
    // a round trip; what travels is the sample the authority simulates from.
    bool aimed = false;
    Query<Read<LookOrientation>, With<LocalLookControl>> aim(world);
    aim.ForEachChunk([&](auto& view) {
        if (aimed || view.Count() == 0)
            return;
        const auto looks = view.template Read<LookOrientation>();
        command.Yaw = looks[0].Yaw;
        command.Pitch = looks[0].Pitch;
        aimed = true;
    });
    if (!aimed)
        return 0;

    // Newest first, which is the order the history ring hands them back and the
    // order the encoder drops from when the window does not fit.
    const std::size_t window =
        std::min<std::size_t>(actions->HistoryCount(), kNetMaxCommandRecords);
    for (std::size_t index = 0; index < window; ++index)
    {
        const InputActionTickRecord source = actions->History(index);
        NetCommandRecord& record = command.Records[index];
        // Renamed onto the authority's clock, keeping the spacing between
        // records: the window has to stay a run of consecutive ticks or the
        // authority reads it as gaps it must fill.
        record.Tick = tickOffset >= 0
            ? source.Tick + static_cast<std::uint64_t>(tickOffset)
            : (static_cast<std::uint64_t>(-tickOffset) > source.Tick
                   ? 0
                   : source.Tick - static_cast<std::uint64_t>(-tickOffset));
        record.ActionCount = static_cast<std::uint8_t>(
            std::min<std::size_t>(source.Values.size(), kNetMaxCommandActions));
        for (std::uint8_t action = 0; action < record.ActionCount; ++action)
            record.Actions[action] = source.Values[action];
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

void PredictionRecordSystem::PostFixed(PostFixedContext& ctx)
{
    if (Prediction == nullptr || Clock == nullptr)
        return;

    const EntityId pawn = Prediction->Predicted();
    if (!pawn.IsValid() || !ctx.Entities.IsAlive(pawn))
        return;
    if (!Clock->HasEstimate())
        return;  // No shared name for this tick, so nothing to record it under.

    if (const LocalTransform* pose = ctx.Entities.TryGet<LocalTransform>(pawn))
    {
        Prediction->Record(Clock->AuthorityTickAt(ctx.Time.TickIndex),
                           pose->Value.Position);
    }
}

void RegisterNetSystems(EngineSchedule& schedule, PeerCommandRuntime& commands,
                        ClientPrediction& prediction,
                        ReplicationInterpolation& interpolation,
                        const NetTickEstimator& clock)
{
    schedule.Register<PeerCommandFeedSystem>(commands);
    schedule.Register<ReplicationInterpolationSystem>(interpolation, prediction, clock);
    schedule.Register<PredictionRecordSystem>(prediction, clock);
}
