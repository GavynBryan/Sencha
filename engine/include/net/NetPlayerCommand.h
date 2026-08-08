#pragma once

#include <input/InputAction.h>
#include <net/ReplicationCodec.h>

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

// One tick of resolved actions.
struct NetCommandRecord
{
    std::uint64_t Tick = 0;
    std::uint8_t ActionCount = 0;
    std::array<InputActionValue, kNetMaxCommandActions> Actions{};

    [[nodiscard]] std::span<const InputActionValue> Values() const
    {
        return std::span(Actions).subspan(0, ActionCount);
    }
};

struct NetPlayerCommand
{
    // Where the player is aiming, as of the newest record. Aim is continuous
    // and integrated on the sender's presentation clock, so it travels once per
    // command rather than once per tick.
    float Yaw = 0.0f;
    float Pitch = 0.0f;

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
//=============================================================================
class NetPeerCommandBuffer
{
public:
    // Ticks a peer can run ahead by. Beyond this the oldest queued record is
    // dropped: a peer that floods is buffering its own latency, and unbounded
    // queueing would let it buy memory on the authority.
    static constexpr std::size_t kCapacity = 16;

    // Takes the records this peer has not been credited with yet. Records at or
    // below the last one consumed are ignored, which is what makes resending
    // the redundancy window free.
    void Receive(const NetPlayerCommand& command);

    // The record this tick simulates. False when nothing has ever arrived --
    // a peer that has not spoken yet has no input, not zeroed input.
    [[nodiscard]] bool Next(NetCommandRecord& out);

    [[nodiscard]] float Yaw() const { return AimYaw; }
    [[nodiscard]] float Pitch() const { return AimPitch; }
    [[nodiscard]] bool HasAim() const { return AimSeen; }

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

    NetCommandRecord Last{};
    bool HasLast = false;
    std::uint64_t LastConsumedTick = 0;
    bool Consumed = false;

    float AimYaw = 0.0f;
    float AimPitch = 0.0f;
    // The newest tick any aim sample arrived with, so a datagram that overtakes
    // a newer one cannot drag the view backwards.
    std::uint64_t AimTick = 0;
    bool AimSeen = false;

    std::uint64_t Starved = 0;
};
