#include <perception/PerceptionComponentRegistration.h>

#include <world/relocation/RelocationRules.h>

#include <algorithm>

namespace
{
    // What it remembered was seen from the side it left.
    void Forget(ObservationMemory& memory, const RelocationFrame&)
    {
        memory.Count = 0;
        std::ranges::fill(memory.Entries, ObservationMemoryEntry{});
    }
}

void RegisterPerceptionRelocation(World& world)
{
    SetRelocationRule<ObservationMemory>(world, RelocationRuleKind::Maps, &RelocateValue<ObservationMemory, &Forget>);
}
