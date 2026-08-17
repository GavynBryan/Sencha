#include <render/ProbeVolumeSlotTable.h>

std::uint32_t ProbeVolumeSlotTable::Acquire()
{
    for (std::uint32_t slot = 0; slot < kCapacity; ++slot)
    {
        if (!Used[slot])
        {
            Used[slot] = true;
            return slot;
        }
    }
    return kInvalidSlot;
}

void ProbeVolumeSlotTable::Release(std::uint32_t slot)
{
    if (slot >= kCapacity)
        return;
    Used[slot] = false;
}

void ProbeVolumeSlotTable::ReleaseAll()
{
    for (bool& used : Used)
        used = false;
}

bool ProbeVolumeSlotTable::IsUsed(std::uint32_t slot) const
{
    return slot < kCapacity && Used[slot];
}

std::uint32_t ProbeVolumeSlotTable::UsedCount() const
{
    std::uint32_t count = 0;
    for (const bool used : Used)
        count += used ? 1u : 0u;
    return count;
}
