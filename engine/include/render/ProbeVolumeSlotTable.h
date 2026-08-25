#pragma once

#include <render/LightGpuTypes.h>

#include <cstdint>

//=============================================================================
// ProbeVolumeSlotTable
//
// Which of the lighting set's probe-volume slots are occupied. Split out of
// ProbeVolumeSet so the residency policy -- first free wins, exhaustion denies
// rather than evicts -- can be read and tested without an image service or a
// descriptor set.
//
// Slot identity is the binding-2 array index the shader samples, so a slot
// number is only meaningful against the LightBindings instance it was acquired
// for. kInvalidSlot is the denial result, never a usable index.
//
// A released slot is not immediately reusable. The frame that sampled it may
// still be executing, and its uniform header names the slot by index, so the
// slot passes through Retiring -- unacquirable, and still pointing at its old
// descriptor -- until the owner proves those frames complete. Three states
// rather than a used flag, because "occupied" and "occupied and reusable next
// frame" are different answers to the only question this table exists to
// answer.
//=============================================================================
class ProbeVolumeSlotTable
{
public:
    static constexpr std::uint32_t kCapacity = kMaxActiveProbeVolumes;
    static constexpr std::uint32_t kInvalidSlot = kCapacity;

    enum class State : std::uint8_t
    {
        Free,      // acquirable
        Resident,  // bound to a volume, named by this frame's headers
        Retiring,  // released, but frames that named it may still execute
    };

    // Lowest free slot, or kInvalidSlot when none is free. Denial is the whole
    // policy: a resident volume is never evicted to make room, since the
    // incoming volume has no claim to be the more important one and the caller
    // can fall back to hemispheric ambient. A retiring slot denies too -- it is
    // occupied by a frame that has not finished with it.
    [[nodiscard]] std::uint32_t Acquire();

    // Resident -> Retiring. The slot stays unacquirable and keeps its
    // descriptor; only Reclaim frees it. Ignores a slot that is not resident,
    // so a zone released twice does not retire someone else's reuse of it.
    void BeginRetire(std::uint32_t slot);

    // -> Free, from any state. The two callers are the reclaim path, once the
    // owner has proven the retiring frames complete, and the acquire-then-fail
    // path, where nothing was ever bound to the slot. Out-of-range is ignored.
    void Release(std::uint32_t slot);

    void ReleaseAll();

    [[nodiscard]] State StateOf(std::uint32_t slot) const;
    // Not free: resident or retiring. What IsFull is counting.
    [[nodiscard]] bool IsUsed(std::uint32_t slot) const;
    [[nodiscard]] std::uint32_t UsedCount() const;
    [[nodiscard]] std::uint32_t CountIn(State state) const;
    [[nodiscard]] bool IsFull() const { return UsedCount() == kCapacity; }

private:
    State States[kCapacity] = {};
};
