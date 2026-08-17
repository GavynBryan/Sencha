#pragma once

#include <render/LightGpuTypes.h>

#include <cstdint>

//=============================================================================
// ProbeVolumeSlotTable
//
// Which of the lighting set's probe-volume slots are occupied. Split out of
// ProbeVolumeSet so the residency policy -- first free wins, exhaustion denies
// rather than evicts, a released slot is immediately reusable -- can be read
// and tested without an image service or a descriptor set.
//
// Slot identity is the binding-2 array index the shader samples, so a slot
// number is only meaningful against the LightBindings instance it was acquired
// for. kInvalidSlot is the denial result, never a usable index.
//=============================================================================
class ProbeVolumeSlotTable
{
public:
    static constexpr std::uint32_t kCapacity = kMaxActiveProbeVolumes;
    static constexpr std::uint32_t kInvalidSlot = kCapacity;

    // Lowest free slot, or kInvalidSlot when every slot is resident. Denial is
    // the whole policy: a resident volume is never evicted to make room, since
    // the incoming volume has no claim to be the more important one and the
    // caller can fall back to hemispheric ambient.
    [[nodiscard]] std::uint32_t Acquire();

    // Releasing a slot that is already free, or one past capacity, is ignored.
    // Teardown paths release per volume and can run twice.
    void Release(std::uint32_t slot);

    void ReleaseAll();

    [[nodiscard]] bool IsUsed(std::uint32_t slot) const;
    [[nodiscard]] std::uint32_t UsedCount() const;
    [[nodiscard]] bool IsFull() const { return UsedCount() == kCapacity; }

private:
    bool Used[kCapacity] = {};
};
