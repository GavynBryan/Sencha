#include <perception/ObservationMemory.h>

const ObservationMemoryEntry* FindRemembered(const ObservationMemory& memory, EntityId subject)
{
    for (std::uint32_t index = 0; index < memory.Count; ++index)
        if (memory.Entries[index].Subject == subject)
            return &memory.Entries[index];
    return nullptr;
}
