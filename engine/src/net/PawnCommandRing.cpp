#include <net/PawnCommandRing.h>

void PawnCommandRing::Push(const PawnCommandTick& record)
{
    if (Count == kDepth)
    {
        // The oldest goes, and the ring remembers losing it: a replay asked to
        // start from before this point cannot be honest about what it skipped.
        LostThrough = Records[Head].Tick;
        Records[Head] = record;
        Head = (Head + 1) % kDepth;
    }
    else
    {
        Records[(Head + Count) % kDepth] = record;
        ++Count;
    }
    Newest = record.Tick;
}

void PawnCommandRing::Clear()
{
    Head = 0;
    Count = 0;
    Newest = 0;
    LostThrough = 0;
}

void PawnCommandRing::DropThrough(std::uint64_t tick)
{
    while (Count > 0 && At(0).Tick <= tick)
    {
        Head = (Head + 1) % kDepth;
        --Count;
    }
}

const PawnCommandTick& PawnCommandRing::Recent(std::size_t index) const
{
    // Newest first, counting back from the end of the window.
    return At(Count - 1 - index);
}
