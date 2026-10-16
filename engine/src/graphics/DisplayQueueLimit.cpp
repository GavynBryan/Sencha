#include <graphics/DisplayQueueLimit.h>

#include <algorithm>

DisplayQueueLimit::DisplayQueueLimit(std::uint32_t initialDepth, std::uint32_t maxDepth)
    : Initial(std::max(initialDepth, 1u))
    , Max(std::max(maxDepth, std::max(initialDepth, 1u)))
    , Current(Initial)
{
}

void DisplayQueueLimit::Reset()
{
    Current = Initial;
    LastId = 0;
    LastStarvedAt.reset();
}

void DisplayQueueLimit::Displayed(const Observation& present, double refreshSeconds)
{
    const bool follows = LastId != 0 && present.Id == LastId + 1;
    const double interval = std::chrono::duration<double>(present.DisplayedAt - LastDisplayedAt).count();
    LastId = present.Id;
    LastDisplayedAt = present.DisplayedAt;
    if (!follows || refreshSeconds <= 0.0)
        return;

    // The display showed the previous frame for more than one refresh although
    // this frame was ready in time: it was released too late, by this limit.
    const bool skippedRefresh = interval > 1.5 * refreshSeconds;
    const bool producedInTime = present.ProductionSeconds < refreshSeconds;
    if (!skippedRefresh || !present.HeldForDisplay || !producedInTime)
        return;

    if (LastStarvedAt && present.DisplayedAt - *LastStarvedAt <= kStarvationPairWindow)
    {
        Current = std::min(Current + 1, Max);
        LastStarvedAt.reset();
        return;
    }
    LastStarvedAt = present.DisplayedAt;
}
