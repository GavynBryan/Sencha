#include <abilities/AbilityCueLog.h>

#include <core/hash/Fnv1a.h>
#include <ecs/World.h>

std::uint16_t AppendAbilityCue(AbilityCueLog& log, AbilityCueEntry entry)
{
    // Zero marks an empty entry, so the serial skips it on wrap.
    if (log.NextSeq == 0)
        log.NextSeq = 1;
    entry.Seq = log.NextSeq++;
    AbilityCueEntry& slot = log.Entries[entry.Seq % kAbilityCueLogCapacity];
    if (slot.Seq != 0)
        ++log.Overwritten;
    slot = entry;
    return entry.Seq;
}

void AppendSourcedCue(World& world, EntityId instigator, std::uint16_t family, EntityId source,
                      AbilityCueEntry entry)
{
    if (!entry.Cue.IsValid() || !world.IsAlive(instigator) || !world.IsRegistered<AbilityCueLog>())
        return;
    AbilityCueLog* log = world.TryGet<AbilityCueLog>(instigator);
    if (log == nullptr)
        return;
    entry.Activation = (static_cast<std::uint64_t>(family) << 48) ^ (static_cast<std::uint64_t>(source.Index) << 24)
                     ^ (entry.Tick & 0xFFFFFFull);
    (void)AppendAbilityCue(*log, entry);
}

std::uint64_t AbilityCueOccurrence(const AbilityCueEntry& entry)
{
    std::uint64_t hash = kFnv1aOffsetBasis;
    HashFnv1aValue(hash, entry.Activation);
    HashFnv1aValue(hash, entry.Op);
    HashFnv1aValue(hash, entry.Tick);
    HashFnv1aValue(hash, entry.Target.Index);
    HashFnv1aValue(hash, entry.Target.Generation);
    HashFnv1aValue(hash, entry.Ordinal);
    return hash;
}
