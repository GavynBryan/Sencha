#include <render/ProbeVolumeSlotTable.h>

std::uint32_t ProbeVolumeSlotTable::Acquire()
{
    for (std::uint32_t slot = 0; slot < kCapacity; ++slot)
    {
        if (States[slot] == State::Free)
        {
            States[slot] = State::Resident;
            return slot;
        }
    }
    return kInvalidSlot;
}

void ProbeVolumeSlotTable::BeginRetire(std::uint32_t slot)
{
    if (slot >= kCapacity || States[slot] != State::Resident)
        return;
    States[slot] = State::Retiring;
}

void ProbeVolumeSlotTable::Release(std::uint32_t slot)
{
    if (slot >= kCapacity)
        return;
    States[slot] = State::Free;
}

void ProbeVolumeSlotTable::ReleaseAll()
{
    for (State& state : States)
        state = State::Free;
}

ProbeVolumeSlotTable::State ProbeVolumeSlotTable::StateOf(std::uint32_t slot) const
{
    return slot < kCapacity ? States[slot] : State::Free;
}

bool ProbeVolumeSlotTable::IsUsed(std::uint32_t slot) const
{
    return slot < kCapacity && States[slot] != State::Free;
}

std::uint32_t ProbeVolumeSlotTable::UsedCount() const
{
    std::uint32_t count = 0;
    for (const State state : States)
        count += state != State::Free ? 1u : 0u;
    return count;
}

std::uint32_t ProbeVolumeSlotTable::CountIn(State state) const
{
    std::uint32_t count = 0;
    for (const State slot : States)
        count += slot == state ? 1u : 0u;
    return count;
}
