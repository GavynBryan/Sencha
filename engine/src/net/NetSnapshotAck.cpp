#include <net/NetSnapshotAck.h>

void NetSnapshotAck::Observe(std::uint32_t sequence)
{
    if (sequence == 0)
        return;  // Zero is the "nothing yet" sentinel, not a snapshot.

    if (NewestSequence == 0)
    {
        NewestSequence = sequence;
        Behind = 0;
        return;
    }

    if (sequence > NewestSequence)
    {
        // The window slides up, and the sequence it was anchored on becomes the
        // first entry behind the new one.
        const std::uint32_t shift = sequence - NewestSequence;
        if (shift < kWindow)
            Behind = (Behind << shift) | (1u << (shift - 1));
        else if (shift == kWindow)
            Behind = 1u << (kWindow - 1);
        else
            Behind = 0;  // Everything the window held has fallen off the back.
        NewestSequence = sequence;
        return;
    }

    if (sequence == NewestSequence)
        return;

    const std::uint32_t back = NewestSequence - sequence;
    if (back <= kWindow)
        Behind |= 1u << (back - 1);
}

void NetSnapshotAck::Merge(const NetSnapshotAck& reported)
{
    if (!reported.Any())
        return;

    // Replayed as individual observations, oldest first, so the shifting is
    // written once and the union is obviously a union. This runs once per
    // arriving command, not per entity.
    for (std::uint32_t back = kWindow; back > 0; --back)
    {
        if ((reported.Behind & (1u << (back - 1))) == 0)
            continue;
        if (reported.NewestSequence <= back)
            continue;  // Would name a sequence before the first one.
        Observe(reported.NewestSequence - back);
    }
    Observe(reported.NewestSequence);
}

bool NetSnapshotAck::Confirms(std::uint32_t sequence) const
{
    if (sequence == 0 || NewestSequence == 0)
        return false;
    if (sequence == NewestSequence)
        return true;
    if (sequence > NewestSequence)
        return false;

    const std::uint32_t back = NewestSequence - sequence;
    if (back > kWindow)
        return false;
    return (Behind & (1u << (back - 1))) != 0;
}

bool NetSnapshotAck::Expired(std::uint32_t sequence) const
{
    if (sequence == 0 || NewestSequence == 0)
        return false;
    return sequence < NewestSequence && (NewestSequence - sequence) > kWindow;
}
