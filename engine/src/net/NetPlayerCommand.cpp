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

    static_assert(kNetMaxCommandActions <= (1u << kActionCountBits),
                  "the action cap has to be expressible in the count field");

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
        std::size_t bits = newest ? 0 : kTickDeltaBits;
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
    const std::size_t headerBits = 32 + 32 + kActionCountBits + 64;
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

    writer.WriteFloat(command.Yaw);
    writer.WriteFloat(command.Pitch);
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
    if (!reader.ReadFloat(out.Yaw) || !reader.ReadFloat(out.Pitch)
        || !reader.ReadBits(kActionCountBits, actions)
        || !reader.ReadU64(newestTick))
    {
        return false;
    }

    if (!std::isfinite(out.Yaw) || !std::isfinite(out.Pitch))
        return false;
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
    if (!AimSeen || newest >= AimTick)
    {
        AimYaw = command.Yaw;
        AimPitch = command.Pitch;
        AimTick = newest;
        AimSeen = true;
    }

    // Oldest first, so the queue fills in order and the insert below almost
    // never has to move anything.
    for (std::size_t index = command.RecordCount; index-- > 0; )
        Insert(command.Records[index]);
}

void NetPeerCommandBuffer::Insert(const NetCommandRecord& record)
{
    // Already simulated. This is the redundancy window doing its job: most of
    // what a command carries is input the authority already has.
    if (Consumed && record.Tick <= LastConsumedTick)
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

        Last = out;
        HasLast = true;
        LastConsumedTick = out.Tick;
        Consumed = true;
        return true;
    }

    // Nothing has ever arrived. A peer that has not spoken has no input, which
    // is not the same as input that is all zero: zeroing would stop a pawn that
    // was never started.
    if (!HasLast)
        return false;

    ++Starved;
    for (std::uint8_t index = 0; index < Last.ActionCount; ++index)
        ClearEdges(Last.Actions[index]);
    out = Last;
    return true;
}
