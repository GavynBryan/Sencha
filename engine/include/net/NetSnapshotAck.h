#pragma once

#include <cstdint>

//=============================================================================
// NetSnapshotAck
//
// Which snapshots a client has actually applied, as evidence rather than as a
// number to be trusted.
//
// A single high-water mark cannot answer the question the authority needs
// answered. "I have applied everything through 40" is a claim about snapshots
// 38 and 39 as much as about 40, and those may never have arrived -- the
// channel is unreliable and drops what it likes. An authority that believed the
// claim would record a peer as holding state it was never sent, and every later
// difference would be measured from a fiction. That is survivable only while
// every snapshot carries the whole world, because the next one overwrites the
// mistake; the moment a snapshot may leave something out, the mistake is
// permanent.
//
// So this carries the newest applied sequence plus a bitmask of the window
// behind it: bit i means "and also the one i+1 before that". Absence of a bit
// is not a denial, only silence -- a client may not have had a chance to
// mention a snapshot yet. Everything here is therefore positive evidence, and
// merging two reports can only ever add to what is known.
//
// Sequences are per peer, start at one, and increase by one per snapshot
// written for that peer. Zero means "nothing yet". They are not simulation
// ticks: a frame can publish twice at one tick, and two datagrams that share a
// name cannot be told apart by an acknowledgement.
//=============================================================================
class NetSnapshotAck
{
public:
    // How far back the window reaches. At any plausible snapshot rate this is
    // most of a second -- far longer than a round trip, which is all the proof
    // has to survive.
    static constexpr std::uint32_t kWindow = 32;

    // Records that `sequence` was applied. Out-of-order arrivals are ordinary:
    // a client applies what reaches it, and what reaches it is not sorted.
    void Observe(std::uint32_t sequence);

    // Folds in what a peer reported. Evidence only: this never lowers the
    // newest sequence and never clears a bit, so a datagram that overtook a
    // newer one costs nothing but cannot undo what the newer one proved.
    void Merge(const NetSnapshotAck& reported);

    // Whether this is proof that `sequence` was applied. False for a sequence
    // older than the window: the proof may have existed and aged out, which is
    // indistinguishable here and has to be treated as unproven.
    [[nodiscard]] bool Confirms(std::uint32_t sequence) const;

    [[nodiscard]] std::uint32_t Newest() const { return NewestSequence; }
    [[nodiscard]] std::uint32_t Window() const { return Behind; }
    [[nodiscard]] bool Any() const { return NewestSequence != 0; }

    void Clear()
    {
        NewestSequence = 0;
        Behind = 0;
    }

    bool operator==(const NetSnapshotAck&) const = default;

private:
    std::uint32_t NewestSequence = 0;
    // Bit i is the sequence (NewestSequence - 1 - i).
    std::uint32_t Behind = 0;
};
