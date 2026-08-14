#pragma once

#include <net/ReplicationLayout.h>

#include <cstddef>
#include <cstdint>
#include <span>

//=============================================================================
// ReplicationCodec
//
// Component bytes to wire bits and back, driven entirely by the compiled
// layout. Pure: no sockets, no services, no allocation, and nothing sized by
// anything a peer said. Like the message decoders, this is a boundary every
// byte from a peer crosses, so it is written to be read by a reviewer and run
// by a fuzzer.
//
// Bits are written least-significant first into ascending bytes, and multi-byte
// values are decomposed arithmetically rather than copied, so the encoding is
// identical on any host regardless of its byte order.
//
// A component is encoded as a field mask followed by the masked fields' values.
// The mask is one bit per field in the layout, in layout order, so the decoder
// needs to know nothing about the receiver to read it: a field the writer chose
// to skip -- unchanged since the baseline, or owner-only and this is not the
// owner -- simply has a zero bit, and the reader skips it for the same reason
// without being told which reason it was.
//=============================================================================

class NetBitWriter
{
public:
    explicit NetBitWriter(std::span<std::byte> buffer) : Buffer(buffer) {}

    // bits must be 1..32. Value bits above `bits` are ignored, never truncated
    // silently into a neighbour: the caller has already clamped.
    void WriteBits(std::uint32_t value, std::uint8_t bits);
    void WriteBool(bool value) { WriteBits(value ? 1u : 0u, 1); }
    void WriteU32(std::uint32_t value) { WriteBits(value, 32); }
    void WriteU64(std::uint64_t value);
    // Seven payload bits at a time, each followed by a bit saying whether
    // another group follows. Small values cost eight bits and the full width
    // costs eighty, which is the right trade for a quantity that is small for
    // most of a session and has no ceiling: entity identities are minted from
    // one and never reused, so they start tiny and grow slowly, and paying
    // sixty-four bits for every one of them was most of what an unchanged
    // entity's envelope cost.
    void WriteVarUInt(std::uint64_t value);
    void WriteFloat(float value);
    void WriteDouble(double value);

    [[nodiscard]] bool Overflowed() const { return Overflow; }
    [[nodiscard]] std::size_t BitsWritten() const { return Cursor; }
    // What is left. A writer that composes optional parts asks before writing
    // one, because a bit writer cannot be rewound once it has overflowed.
    [[nodiscard]] std::size_t BitsRemaining() const
    {
        return Overflow ? 0 : Buffer.size() * 8 - Cursor;
    }
    // Rounded up to whole bytes; the tail bits of the last byte are zero.
    [[nodiscard]] std::size_t BytesWritten() const { return (Cursor + 7) / 8; }
    [[nodiscard]] std::span<const std::byte> Written() const
    {
        return Buffer.subspan(0, BytesWritten());
    }

private:
    std::span<std::byte> Buffer;
    std::size_t Cursor = 0;  // in bits
    bool Overflow = false;
};

class NetBitReader
{
public:
    explicit NetBitReader(std::span<const std::byte> buffer) : Buffer(buffer) {}

    // Every read reports whether it had the bits. A caller that ignores the
    // result reads zeros rather than garbage, and Overflowed() stays set, so a
    // truncated message cannot be mistaken for a valid one.
    [[nodiscard]] bool ReadBits(std::uint8_t bits, std::uint32_t& out);
    [[nodiscard]] bool ReadBool(bool& out);
    [[nodiscard]] bool ReadU32(std::uint32_t& out) { return ReadBits(32, out); }
    [[nodiscard]] bool ReadU64(std::uint64_t& out);
    [[nodiscard]] bool ReadFloat(float& out);
    [[nodiscard]] bool ReadDouble(double& out);
    // Bounded at ten groups, which is every bit a u64 has. A stream claiming an
    // eleventh is refused rather than read: continuation bits come from a peer,
    // and an unbounded loop over them is a peer deciding how long this runs.
    [[nodiscard]] bool ReadVarUInt(std::uint64_t& out);

    [[nodiscard]] bool Overflowed() const { return Overflow; }
    [[nodiscard]] std::size_t BitsRead() const { return Cursor; }
    [[nodiscard]] std::size_t BitsRemaining() const
    {
        return Buffer.size() * 8 - Cursor;
    }

private:
    std::span<const std::byte> Buffer;
    std::size_t Cursor = 0;  // in bits
    bool Overflow = false;
};

//-----------------------------------------------------------------------------
// Quantization
//
// A value outside the declared range clamps to it. Clamping is the honest
// failure: a wrapped value would put an entity somewhere it has never been,
// while a clamped one pins it at the edge of the range someone declared, which
// is visible and traceable to the declaration.
//-----------------------------------------------------------------------------
[[nodiscard]] std::uint32_t ReplicationQuantize(float value,
                                                const FieldQuantization& range);
[[nodiscard]] float ReplicationDequantize(std::uint32_t quantized,
                                          const FieldQuantization& range);

// Snaps every quantized field of a component to the value the wire would carry.
// The authority applies this before it stores a baseline or hashes state, so
// that what it believes a client has is exactly what the client got, and a
// desync comparison reports real divergence rather than rounding.
void ReplicationSnapToWire(const ReplicatedComponent& component,
                           std::span<std::byte> componentBytes);

// Folds the named field runs of one component into a rolling hash, so two
// machines can say whether they hold the same value without sending the value.
//
// Only the bytes the fields declare, run by run. Never the whole component: the
// padding between two fields is uninitialized on both sides and folding it
// would report divergence in bytes neither machine has an opinion about.
//
// The caller chooses the mask, and the choice is the whole design. Fold what
// the peer was eligible to receive -- ReplicationVisibleFields answers that per
// peer -- because an owner-only field withheld from a non-owner is a field both
// sides are correct to disagree about, and a check that fires on those gets
// turned off, which is worse than not having it.
//
// Bytes are expected already snapped to wire precision on the authority's side.
// Order matters: the same fields folded in a different order give a different
// hash, which is why both sides walk the layout rather than their own storage.
[[nodiscard]] std::uint64_t ReplicationFoldFields(
    std::uint64_t hash, const ReplicatedComponent& component,
    std::span<const std::byte> componentBytes, std::uint64_t fields);

//-----------------------------------------------------------------------------
// Component encode and decode
//
// Which fields travel is the caller's decision, handed over as a mask of field
// runs. That is deliberate: whether a field has changed is a question about the
// authority's history, and whether a peer may see it is a question about that
// peer -- neither is something a codec can answer, and folding them in here is
// what made the two get confused for each other.
//
// The two halves of the answer are below, so a caller composes them rather than
// reinventing either.
//
// Returns false if the component did not fit, which is a budgeting error on the
// authority and never a wire condition.
//-----------------------------------------------------------------------------
[[nodiscard]] bool ReplicationEncodeComponent(const ReplicatedComponent& component,
                                              std::span<const std::byte> current,
                                              std::uint64_t fields,
                                              NetBitWriter& writer);

// Exactly what the call above would write for the same mask, without writing it.
// A caller filling a fixed budget needs this because a bit writer cannot be
// rewound: an entity that turns out not to fit part way through cannot be taken
// back out, and a half-written entity is not a smaller snapshot, it is a corrupt
// one. The value depends on the mask, not on the bytes.
[[nodiscard]] std::size_t ReplicationEncodedComponentBits(
    const ReplicatedComponent& component, std::uint64_t fields);

// Which of a component's field runs the wire would carry differently. An empty
// `previous` means every run: there is nothing to difference against, which is
// the fresh-state form. Quantized runs compare at wire precision, so movement
// finer than the declared resolution is not a change.
[[nodiscard]] std::uint64_t ReplicationChangedFields(
    const ReplicatedComponent& component,
    std::span<const std::byte> current,
    std::span<const std::byte> previous);

// Which of a component's field runs a particular peer is allowed to receive:
// owner-only runs to the owner, owner-local runs to everyone else. A fact about
// the receiver, not about the value.
[[nodiscard]] std::uint64_t ReplicationVisibleFields(
    const ReplicatedComponent& component, bool forOwner);

// Applies a decoded component onto `target`, which must already hold the
// receiver's current value for this component: fields whose mask bit is clear
// are left exactly as they were, which is what makes a delta a delta.
//
// Returns false on a truncated or malformed message. The target is not
// guaranteed untouched on failure -- callers decode into staging bytes and
// commit only on success.
[[nodiscard]] bool ReplicationDecodeComponent(const ReplicatedComponent& component,
                                              NetBitReader& reader,
                                              std::span<std::byte> target);

// Widest a single component can be on the wire: the mask plus every field at
// full width. Callers size staging buffers from this rather than from anything
// a peer said.
[[nodiscard]] std::size_t ReplicationMaxComponentBits(
    const ReplicatedComponent& component);
