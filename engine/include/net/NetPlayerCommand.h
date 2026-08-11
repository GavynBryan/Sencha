#pragma once

#include <input/InputAction.h>
#include <net/NetSnapshotAck.h>
#include <net/ReplicationCodec.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

//=============================================================================
// NetPlayerCommand
//
// One player's request, on the wire.
//
// The shape is the usercmd every authoritative shooter has had since Quake: the
// actions a player took and where they were aiming, and nothing about where
// they think they are. The authority simulates from it, so a client that lies
// gets a different lie simulated, not a different position.
//
// It carries actions rather than anything derived from them because deciding
// what an action means is the game's job and the game runs on the authority. A
// client that framed its own movement would be deciding its own speed, and no
// request it never framed -- a jump, a fire, an ability -- could reach the
// authority at all.
//
// Actions ride as a dense array indexed the way the resolved action state is
// indexed. Both ends run the same build against the same content, so the
// vocabulary matches for the same reason the component tables do; a mismatch
// scrambles that client's own controls and reaches nothing else, because the
// authority validates every effect an action can have.
//
// The channel is unreliable, so a command carries the last few ticks rather
// than one. A dropped datagram then costs nothing as long as the next arrives,
// which is far cheaper than making input reliable and paying a retransmit
// stall on the one thing that cannot wait.
//=============================================================================

// Decode-boundary caps. Sized from what the format allows, never from anything
// a peer said, and small enough that the worst case a peer can force still fits
// one datagram.
inline constexpr std::size_t kNetMaxCommandActions = 32;
inline constexpr std::size_t kNetMaxCommandRecords = 8;

// One tick of resolved actions, and the aim they were taken with.
//
// Aim rides per tick because movement is derived from it: the direction a
// player asks for is their input framed by where they are looking, so a tick
// simulated against a different aim walks a different path. Sending one sample
// per datagram and applying it to every tick in the window made the two
// machines answer different questions -- against a wall or a corner, that is
// the difference between stopping and not.
struct NetCommandRecord
{
    std::uint64_t Tick = 0;
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    std::uint8_t ActionCount = 0;
    std::array<InputActionValue, kNetMaxCommandActions> Actions{};

    [[nodiscard]] std::span<const InputActionValue> Values() const
    {
        return std::span(Actions).subspan(0, ActionCount);
    }
};

struct NetPlayerCommand
{
    // Which snapshots this client has applied. It travels back with the input
    // because the input already travels every tick, and because the authority
    // cannot compute a correct difference without it: a snapshot that was
    // written is not a snapshot that arrived, and a delta measured from one the
    // client never received describes a state it was never in.
    //
    // Evidence, not a high-water mark -- see NetSnapshotAck. A mark alone would
    // vouch for the snapshots lost behind it.
    NetSnapshotAck SnapshotAck;

    std::uint8_t RecordCount = 0;
    // Newest first. The rest are the redundancy window: ticks the authority may
    // already have, resent so that losing one datagram loses no input.
    std::array<NetCommandRecord, kNetMaxCommandRecords> Records{};

    [[nodiscard]] std::span<const NetCommandRecord> Ticks() const
    {
        return std::span(Records).subspan(0, RecordCount);
    }
};

// Writes as many records as fit, newest first, and reports how many went. A
// command that cannot fit even its newest record writes nothing and returns
// zero: dropping the oldest redundancy is a graceful loss, and truncating a
// record would be a corrupt one.
std::size_t NetEncodePlayerCommand(const NetPlayerCommand& command,
                                   NetBitWriter& writer);

// The same message carrying an acknowledgement and no input.
//
// A client with nothing to say still has something to confirm, and until this
// existed it could not: the command message was the only thing carrying a
// snapshot acknowledgement, and it refused to encode without a record. So a
// client that had not been given a pawn yet -- which is exactly the join
// window, and every spectator for as long as it spectates -- never advanced a
// floor, and the authority went on sending it whole entities at roughly twice
// a delta's cost, during the one stretch where it has the least room for them.
[[nodiscard]] bool NetEncodeCommandAck(const NetSnapshotAck& ack,
                                       NetBitWriter& writer);

// Every field a peer controls is bounded here: counts against the caps above,
// and floats rejected unless finite. A caller that gets false has a malformed
// message and nothing half-applied.
[[nodiscard]] bool NetDecodePlayerCommand(NetBitReader& reader,
                                          NetPlayerCommand& out);

//=============================================================================
// NetPeerCommandBuffer
//
// One peer's arriving commands, held until the ticks that consume them.
//
// Commands arrive when datagrams do, which is once per frame at best, in
// bursts, out of order, and with the redundancy window repeating ticks the
// authority already has. Ticks consume them one at a time. The buffer is what
// makes those two rates meet: it dedupes by tick, orders by tick, and hands out
// exactly one record per tick.
//
// When it runs dry the last record repeats with its edges cleared. Holding what
// was held is right -- a player holding forward through a dropped packet is
// still holding forward -- while repeating a press would fire the same jump
// again on every starved tick.
//
// It also keeps itself shallow. A queued record is not input the simulation
// still owes anyone; it is standing latency, paid again on every tick that
// follows, because records arrive at the same rate ticks consume them and a
// deep queue therefore never drains on its own. So first contact takes only
// the newest record -- the window behind it is insurance for ticks this
// authority never ran -- and depth that persists above a small slack is
// collapsed into the next record handed out, edges carried, so shedding
// backlog cannot eat a tap that lived in a shed tick.
//
// The target depth is a ceiling and not a floor, which is easy to read the
// wrong way round. The queue fills on its own from the ticks that arrive
// several at a time on a jittery link, and the collapse is what stops it
// filling past the target; nothing holds a record back to build depth
// deliberately. Adding that costs more than it buys -- every dry spell would be
// followed by the target's worth of further starved ticks while the depth was
// rebuilt, on exactly the connections that run dry most.
//=============================================================================
class NetPeerCommandBuffer
{
public:
    // Ticks a peer can run ahead by. Beyond this the oldest queued record is
    // dropped: a peer that floods is buffering its own latency, and unbounded
    // queueing would let it buy memory on the authority.
    static constexpr std::size_t kCapacity = 16;

    // Depth the buffer tolerates in steady state. One record of slack rides
    // out a late datagram without starving; every record above it is one tick
    // of lag the player pays on every input from then on. The default; the
    // authority tunes it live against what the stats surface reports.
    static constexpr std::size_t kTargetDepth = 1;

    // Consecutive consumes that must end above the target before the excess is
    // collapsed. A frame that runs several ticks legitimately drains a
    // several-deep queue inside the frame -- depth alone cannot tell that burst
    // from backlog, but the burst falls with every consume and backlog does
    // not, which is what a streak measures.
    static constexpr std::size_t kCollapseStreak = 3;

    // Takes the records this peer has not been credited with yet. Records
    // below the admission floor -- consumed already, or older than first
    // contact -- are ignored, which is what makes resending the redundancy
    // window free.
    void Receive(const NetPlayerCommand& command);

    // The record this tick simulates. False when nothing has ever arrived --
    // a peer that has not spoken yet has no input, not zeroed input.
    [[nodiscard]] bool Next(NetCommandRecord& out);

    // Trading latency for tolerance of a jittery connection. Zero consumes as
    // fast as records arrive and starves on the first late one; anything above
    // that is lag every input pays for the calm it buys.
    void SetTargetDepth(std::size_t ticks)
    {
        Target = std::min(ticks, kCapacity - 1);
    }
    [[nodiscard]] std::size_t TargetDepth() const { return Target; }

    // What this peer has proved it holds, merged across every command that has
    // arrived. Only ever gains: a datagram that overtakes a newer one carries
    // an older view, which can add to what is known and never subtract.
    [[nodiscard]] const NetSnapshotAck& SnapshotAck() const { return AckedSnapshot; }

    // The newest tick this peer will never be asked about again: everything at
    // or below it has been simulated or passed over for good. The client is
    // told this in every snapshot, and it is what tells it which of its own
    // records it still owes a replay.
    [[nodiscard]] std::uint64_t ConsumedThrough() const
    {
        return AdmitFloor == 0 ? 0 : AdmitFloor - 1;
    }

    // Ticks handed out with nothing queued behind them. The starvation rate is
    // what a stats panel reads to say a peer's connection is behind.
    [[nodiscard]] std::uint64_t StarvedTicks() const { return Starved; }
    [[nodiscard]] std::size_t QueuedTicks() const { return Count; }

private:
    void Insert(const NetCommandRecord& record);

    // Sorted by tick, oldest at Head. A ring rather than a map: the capacity is
    // tiny and fixed, and inserting in order beats hashing a value that arrives
    // in order almost every time.
    std::array<NetCommandRecord, kCapacity> Queue{};
    std::size_t Head = 0;
    std::size_t Count = 0;

    std::size_t Target = kTargetDepth;

    NetCommandRecord Last{};
    bool HasLast = false;
    // Records below this tick are never queued: they are either consumed
    // already or older than first contact, which makes them latency, not input.
    std::uint64_t AdmitFloor = 0;
    NetSnapshotAck AckedSnapshot;
    bool SeenCommand = false;
    // Consecutive consumes that ended above the target. Reset the moment depth
    // is healthy, so only depth that persists ever reads as backlog.
    std::size_t BacklogTicks = 0;

    std::uint64_t Starved = 0;
};
