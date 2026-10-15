#include <abilities/AbilityCueLog.h>

#include <core/hash/Fnv1a.h>

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

std::uint64_t AbilityCueOccurrence(const AbilityCueEntry& entry)
{
    std::uint64_t hash = kFnv1aOffsetBasis;
    HashFnv1aValue(hash, entry.Activation);
    HashFnv1aValue(hash, entry.Op);
    HashFnv1aValue(hash, entry.Tick);
    return hash;
}
