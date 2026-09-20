#include <authored/VerbTrace.h>

#include <algorithm>

VerbTraceRing::VerbTraceRing(std::size_t capacity)
    // A ring with no room would silently record nothing, which reads in an
    // inspector exactly like a dispatcher that was never called.
    : Entries(std::max<std::size_t>(capacity, 1))
{
}

void VerbTraceRing::Record(const VerbTraceRecord& record)
{
    if (Live == Entries.size())
        ++Dropped;
    else
        ++Live;

    Entries[Next] = record;
    Next = (Next + 1) % Entries.size();
}

std::vector<VerbTraceRecord> VerbTraceRing::Snapshot() const
{
    std::vector<VerbTraceRecord> ordered;
    ordered.reserve(Live);
    const std::size_t oldest = (Next + Entries.size() - Live) % Entries.size();
    for (std::size_t offset = 0; offset < Live; ++offset)
        ordered.push_back(Entries[(oldest + offset) % Entries.size()]);
    return ordered;
}

void VerbTraceRing::Clear()
{
    Next = 0;
    Live = 0;
    Dropped = 0;
}
