#include <abilities/AbilityCueLog.h>

#include <core/hash/Fnv1a.h>

#include <algorithm>

std::uint16_t AppendAbilityCue(AbilityCueLog& log, AbilityCueEntry entry)
{
    // Zero marks an empty entry, so the serial skips it on wrap.
    if (log.NextSeq == 0)
        log.NextSeq = 1;
    entry.Ordinal = 0;
    for (const AbilityCueEntry& earlier : log.Entries)
        if (earlier.Seq != 0 && earlier.Activation == entry.Activation && earlier.Op == entry.Op
            && earlier.Tick == entry.Tick && earlier.Target == entry.Target)
            entry.Ordinal = static_cast<std::uint8_t>(std::clamp<int>(earlier.Ordinal + 1, entry.Ordinal, 0xFF));
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
    HashFnv1aValue(hash, entry.Target.Index);
    HashFnv1aValue(hash, entry.Target.Generation);
    HashFnv1aValue(hash, entry.Ordinal);
    return hash;
}
