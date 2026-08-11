#include <net/NetPlayerCommand.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    // Widths. Six bits carry the action cap, eight carry how far back a record
    // sits from the newest one in the same command.
    constexpr std::uint8_t kActionCountBits = 6;
    constexpr std::uint8_t kTickDeltaBits = 8;
    constexpr std::uint8_t kFlagBits = 4;
    constexpr std::uint64_t kMaxTickDelta = 255;

    // Yaw travels at full width: it is a running total with no bound the input
    // layer promises, so a range would silently wrap someone who kept turning.
    // Pitch is bounded by the neck it describes, so sixteen bits over that
    // range is finer than any display can show.
    constexpr std::uint8_t kPitchBits = 16;
    constexpr float kPitchLimit = 1.5707964f;
    constexpr std::size_t kAimBits = 32 + kPitchBits;

    static_assert(kNetMaxCommandActions <= (1u << kActionCountBits),
                  "the action cap has to be expressible in the count field");

    void WritePitch(float pitch, NetBitWriter& writer)
    {
        const float clamped = std::clamp(pitch, -kPitchLimit, kPitchLimit);
        const float unit = (clamped + kPitchLimit) / (2.0f * kPitchLimit);
        constexpr std::uint32_t steps = (1u << kPitchBits) - 1u;
        writer.WriteBits(
            static_cast<std::uint32_t>(unit * static_cast<float>(steps) + 0.5f),
            kPitchBits);
    }

    [[nodiscard]] bool ReadPitch(NetBitReader& reader, float& pitch)
    {
        std::uint32_t raw = 0;
        if (!reader.ReadBits(kPitchBits, raw))
            return false;
        constexpr std::uint32_t steps = (1u << kPitchBits) - 1u;
        const float unit = static_cast<float>(raw) / static_cast<float>(steps);
        pitch = unit * (2.0f * kPitchLimit) - kPitchLimit;
        return true;
    }

    [[nodiscard]] bool HasAxis(const InputActionValue& value)
    {
        return value.X != 0.0f || value.Y != 0.0f;
    }

    // Exactly what one record costs, including the bit that says whether
    // another follows it. Axes are counted rather than assumed because most
    // actions are buttons, and a bound that assumed otherwise would refuse
    // redundancy that fits comfortably.
    [[nodiscard]] std::size_t RecordBits(const NetCommandRecord& record,
                                         std::uint8_t actionCount,
                                         bool newest)
    {
        std::size_t bits = (newest ? 0 : kTickDeltaBits) + kAimBits;
        for (std::uint8_t index = 0; index < actionCount; ++index)
        {
            bits += kFlagBits + 1;
            if (HasAxis(record.Actions[index]))
                bits += 64;
        }
        return bits + 1;
    }

    void WriteRecord(const NetCommandRecord& record, std::uint8_t actionCount,
                     NetBitWriter& writer)
    {
        writer.WriteFloat(record.Yaw);
        WritePitch(record.Pitch, writer);

        for (std::uint8_t index = 0; index < actionCount; ++index)
        {
            const InputActionValue& value = record.Actions[index];
            writer.WriteBits(static_cast<std::uint32_t>(value.Flags), kFlagBits);

            // Axes travel at full width. An action's magnitude is not bounded
            // by anything the input layer promises -- a pointer delta is an
            // axis too -- so a quantized range would silently clamp a value the
            // sender meant.
            const bool axis = HasAxis(value);
            writer.WriteBool(axis);
            if (axis)
            {
                writer.WriteFloat(value.X);
                writer.WriteFloat(value.Y);
            }
        }
    }

    [[nodiscard]] bool ReadRecord(NetBitReader& reader, std::uint8_t actionCount,
                                  NetCommandRecord& record)
    {
        record.ActionCount = actionCount;
        if (!reader.ReadFloat(record.Yaw) || !ReadPitch(reader, record.Pitch))
            return false;
        // A non-finite aim frames every wish direction derived from it, so it
        // is refused at the boundary rather than downstream.
        if (!std::isfinite(record.Yaw))
            return false;

        for (std::uint8_t index = 0; index < actionCount; ++index)
        {
            std::uint32_t flags = 0;
            if (!reader.ReadBits(kFlagBits, flags))
                return false;

            InputActionValue value;
            value.Flags = static_cast<InputActionFlags>(flags);

            bool axis = false;
            if (!reader.ReadBool(axis))
                return false;
            if (axis)
            {
                if (!reader.ReadFloat(value.X) || !reader.ReadFloat(value.Y))
                    return false;
                // A non-finite axis poisons every consumer downstream of it and
                // is the first thing anyone sends when probing a server.
                if (!std::isfinite(value.X) || !std::isfinite(value.Y))
                    return false;
            }
            record.Actions[index] = value;
        }
        return true;
    }

    // What a starved tick keeps.
    //
    // Held survives: a player holding forward through a dropped datagram is
    // still holding forward. The edges do not: repeating a press would fire the
    // same jump again on every starved tick. Fired is the action's own mode
    // stamped by the sender, and the receiver does not know which mode that
    // was -- but a fire that is still held is a hold-mode fire and belongs with
    // the hold, while one that is not is an edge and goes with the edges.
    void ClearEdges(InputActionValue& value)
    {
        const bool held = value.IsHeld();
        InputActionFlags flags = InputActionFlags::None;
        if (held)
        {
            flags |= InputActionFlags::Held;
            if (value.WasFired())
                flags |= InputActionFlags::Fired;
        }
        value.Flags = flags;
    }

    // What a collapsed tick keeps. Levels and axes come from the newest record
    // of the batch -- they are states, and the newest state is the true one --
    // but the edges are OR'd forward, because a press that lived only in a
    // shed tick is a request the player made exactly once and shedding backlog
    // must not be how it disappears.
    void CarryEdges(const NetCommandRecord& older, NetCommandRecord& newer)
    {
        constexpr std::uint8_t edgeMask =
            static_cast<std::uint8_t>(InputActionFlags::Pressed)
            | static_cast<std::uint8_t>(InputActionFlags::Released)
            | static_cast<std::uint8_t>(InputActionFlags::Fired);

        const std::uint8_t count = std::min(older.ActionCount, newer.ActionCount);
        for (std::uint8_t index = 0; index < count; ++index)
        {
            const std::uint8_t edges =
                static_cast<std::uint8_t>(older.Actions[index].Flags) & edgeMask;
            newer.Actions[index].Flags = static_cast<InputActionFlags>(
                static_cast<std::uint8_t>(newer.Actions[index].Flags) | edges);
        }
    }
}

std::size_t NetEncodePlayerCommand(const NetPlayerCommand& command,
                                   NetBitWriter& writer)
{
    if (command.RecordCount == 0)
        return 0;

    const std::uint8_t actions = static_cast<std::uint8_t>(
        std::min<std::size_t>(command.Records[0].ActionCount, kNetMaxCommandActions));
    const std::uint64_t newestTick = command.Records[0].Tick;

    // The whole set is decided before any of it is written. Each record ends
    // with a bit saying whether another follows, and a bit writer cannot be
    // rewound to correct one: running out halfway would leave a message
    // claiming input it does not carry.
    const std::size_t headerBits = kActionCountBits + 64 + 64;
    std::size_t budget = writer.BitsRemaining();
    if (budget < headerBits)
        return 0;
    budget -= headerBits;

    std::size_t count = 0;
    const std::size_t available =
        std::min<std::size_t>(command.RecordCount, kNetMaxCommandRecords);
    for (std::size_t index = 0; index < available; ++index)
    {
        const NetCommandRecord& record = command.Records[index];
        // Older than the window can express, so this record and everything
        // behind it is dropped. Redundancy is a courtesy; the newest record is
        // the message.
        if (index > 0
            && (record.Tick > newestTick || newestTick - record.Tick > kMaxTickDelta))
        {
            break;
        }

        const std::size_t cost = RecordBits(record, actions, index == 0);
        if (cost > budget)
            break;
        budget -= cost;
        ++count;
    }

    if (count == 0)
        return 0;

    writer.WriteBits(command.SnapshotAck.Newest(), 32);
    writer.WriteBits(command.SnapshotAck.Window(), 32);
    writer.WriteBits(actions, kActionCountBits);
    writer.WriteU64(newestTick);

    for (std::size_t index = 0; index < count; ++index)
    {
        if (index > 0)
        {
            writer.WriteBits(
                static_cast<std::uint32_t>(newestTick - command.Records[index].Tick),
                kTickDeltaBits);
        }
        WriteRecord(command.Records[index], actions, writer);
        writer.WriteBool(index + 1 < count);
    }

    return writer.Overflowed() ? 0 : count;
}

bool NetDecodePlayerCommand(NetBitReader& reader, NetPlayerCommand& out)
{
    out = NetPlayerCommand{};

    std::uint32_t actions = 0;
    std::uint64_t newestTick = 0;
    std::uint32_t ackNewest = 0;
    std::uint32_t ackWindow = 0;
    if (!reader.ReadBits(32, ackNewest)
        || !reader.ReadBits(32, ackWindow)
        || !reader.ReadBits(kActionCountBits, actions)
        || !reader.ReadU64(newestTick))
    {
        return false;
    }
    // Rebuilt through the type's own accumulation so a peer cannot hand over a
    // window that names sequences ahead of the one it claims is newest.
    out.SnapshotAck.Observe(ackNewest);
    for (std::uint32_t back = NetSnapshotAck::kWindow; back > 0; --back)
    {
        if ((ackWindow & (1u << (back - 1))) != 0 && ackNewest > back)
            out.SnapshotAck.Observe(ackNewest - back);
    }

    if (actions > kNetMaxCommandActions)
        return false;

    const std::uint8_t actionCount = static_cast<std::uint8_t>(actions);
    for (std::size_t index = 0; index < kNetMaxCommandRecords; ++index)
    {
        NetCommandRecord& record = out.Records[index];
        if (index == 0)
        {
            record.Tick = newestTick;
        }
        else
        {
            std::uint32_t delta = 0;
            if (!reader.ReadBits(kTickDeltaBits, delta))
                return false;
            // Deltas run backwards from the newest tick, so a record can never
            // claim to be newer than the one that named the tick.
            if (delta > newestTick)
                return false;
            record.Tick = newestTick - delta;
        }

        if (!ReadRecord(reader, actionCount, record))
            return false;
        out.RecordCount = static_cast<std::uint8_t>(index + 1);

        bool more = false;
        if (!reader.ReadBool(more))
            return false;
        if (!more)
            return true;
    }

    // The continuation bit was still set at the cap: a sender claiming more
    // records than the format allows.
    return false;
}

void NetPeerCommandBuffer::Receive(const NetPlayerCommand& command)
{
    if (command.RecordCount == 0)
        return;

    const std::uint64_t newest = command.Records[0].Tick;
    AckedSnapshot.Merge(command.SnapshotAck);

    // First contact takes the newest record only. The window behind it is
    // insurance for ticks this authority never ran; queueing it would seed a
    // backlog exactly one window deep, and since records then arrive at the
    // same rate ticks consume them, that backlog would never drain -- every
    // command this peer ever sends would be simulated a full window late.
    if (!SeenCommand)
    {
        SeenCommand = true;
        AdmitFloor = newest;
    }

    // Oldest first, so the queue fills in order and the insert below almost
    // never has to move anything.
    for (std::size_t index = command.RecordCount; index-- > 0; )
        Insert(command.Records[index]);
}

void NetPeerCommandBuffer::Insert(const NetCommandRecord& record)
{
    // Below the floor is input nobody wants: consumed already, or older than
    // first contact. This is the redundancy window doing its job -- most of
    // what a command carries is insurance the authority did not need.
    if (record.Tick < AdmitFloor)
        return;

    for (std::size_t index = 0; index < Count; ++index)
    {
        if (Queue[(Head + index) % kCapacity].Tick == record.Tick)
            return;
    }

    if (Count == kCapacity)
    {
        // A peer running this far ahead is buffering its own latency, and the
        // oldest record is the one the authority is least likely to still want.
        Head = (Head + 1) % kCapacity;
        --Count;
    }

    Queue[(Head + Count) % kCapacity] = record;
    ++Count;

    for (std::size_t index = Count - 1; index > 0; --index)
    {
        NetCommandRecord& previous = Queue[(Head + index - 1) % kCapacity];
        NetCommandRecord& current = Queue[(Head + index) % kCapacity];
        if (previous.Tick <= current.Tick)
            break;
        std::swap(previous, current);
    }
}

bool NetPeerCommandBuffer::Next(NetCommandRecord& out)
{
    if (Count > 0)
    {
        out = Queue[Head];
        Head = (Head + 1) % kCapacity;
        --Count;

        // Catch-up. Depth above the slack that persists across consumes is
        // backlog -- a stall's burst, or a peer whose clock runs slightly fast
        // -- and every record of it is a tick of lag paid on every input from
        // now on. It is shed here in one consume, collapsed into the record
        // handed out. The streak is what keeps a frame that runs several ticks
        // safe: its burst falls with every consume and never reads as backlog.
        if (Count > Target)
        {
            ++BacklogTicks;
            if (BacklogTicks >= kCollapseStreak)
            {
                while (Count > Target)
                {
                    NetCommandRecord newer = Queue[Head];
                    Head = (Head + 1) % kCapacity;
                    --Count;
                    CarryEdges(out, newer);
                    out = newer;
                }
                BacklogTicks = 0;
            }
        }
        else
        {
            BacklogTicks = 0;
        }

        Last = out;
        HasLast = true;
        AdmitFloor = out.Tick + 1;
        return true;
    }

    // Nothing has ever arrived. A peer that has not spoken has no input, which
    // is not the same as input that is all zero: zeroing would stop a pawn that
    // was never started.
    if (!HasLast)
        return false;

    BacklogTicks = 0;
    ++Starved;
    for (std::uint8_t index = 0; index < Last.ActionCount; ++index)
        ClearEdges(Last.Actions[index]);
    out = Last;
    return true;
}
