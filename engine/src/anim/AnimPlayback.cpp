#include <anim/AnimPlayback.h>

double AnimElapsedSeconds(const AnimPlayback& playback, AnimTick tick, double tickSeconds)
{
    const AnimTick ticks = tick >= playback.StartTick ? tick - playback.StartTick : 0;
    return static_cast<double>(playback.OffsetSeconds)
        + static_cast<double>(ticks) * tickSeconds * static_cast<double>(playback.Rate);
}

double AnimPlaceSeconds(double elapsed, double duration, bool cyclic)
{
    if (duration <= 0.0)
        return 0.0;
    if (!cyclic)
        return std::clamp(elapsed, 0.0, duration);
    const double wrapped = std::fmod(elapsed, duration);
    return wrapped < 0.0 ? wrapped + duration : wrapped;
}

double AnimPlaybackSeconds(const AnimPlayback& playback, AnimTick tick, double tickSeconds)
{
    return AnimPlaceSeconds(AnimElapsedSeconds(playback, tick, tickSeconds),
                            static_cast<double>(playback.DurationSeconds), playback.Cyclic);
}

AnimTick AnimPlaybackEndTick(const AnimPlayback& playback, double tickSeconds)
{
    if (playback.Cyclic || playback.Rate == 0.0f)
        return kAnimNoTick;
    const double remaining = playback.Rate > 0.0f
        ? static_cast<double>(playback.DurationSeconds) - static_cast<double>(playback.OffsetSeconds)
        : static_cast<double>(playback.OffsetSeconds);
    const double perTick = tickSeconds * std::abs(static_cast<double>(playback.Rate));
    // A rounding short of a whole tick still ends on it.
    const double ticks = perTick > 0.0 ? std::ceil(remaining / perTick - 1e-9) : 1.0;
    return playback.StartTick + std::max<AnimTick>(1, static_cast<AnimTick>(std::max(ticks, 1.0)));
}

bool AnimPlaybackEnded(const AnimPlayback& playback, AnimTick tick, double tickSeconds)
{
    const AnimTick end = AnimPlaybackEndTick(playback, tickSeconds);
    return end != kAnimNoTick && tick >= end;
}

AnimCrossing AnimCrossPlayback(const AnimPlayback& playback, AnimTick through, AnimPlaybackEntry entry,
                               AnimTick now, double tickSeconds)
{
    AnimCrossing crossing;
    const auto add = [&](double from, double to, bool includeFrom, bool skipped) {
        crossing.Stretches[crossing.Count++] = AnimCrossedStretch{ from, to, includeFrom, skipped };
    };
    const double at = AnimElapsedSeconds(playback, now, tickSeconds);
    const double before = now > 0 ? AnimElapsedSeconds(playback, now - 1, tickSeconds) : at;

    if (through != kAnimNoTick)
    {
        if (now <= through)
            return crossing;
        if (through + 1 < now)
            add(AnimElapsedSeconds(playback, through, tickSeconds), before, false, true);
        add(before, at, false, false);
        return crossing;
    }

    const double start = static_cast<double>(playback.OffsetSeconds);
    const bool carried = entry == AnimPlaybackEntry::Carried;
    const double from = carried ? start - tickSeconds * static_cast<double>(playback.Rate) : start;
    if (now <= playback.StartTick + 1)
    {
        add(from, at, !carried, false);
        return crossing;
    }
    add(from, before, !carried, true);
    add(before, at, false, false);
    return crossing;
}
