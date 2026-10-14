#include <anim/AnimPlayback.h>

namespace
{
    struct Anchor
    {
        AnimTick Tick;
        double Seconds;
    };

    Anchor AnchorOf(const AnimPlayback& playback)
    {
        if (playback.RetimedTick != kAnimNoTick)
            return { playback.RetimedTick, playback.RetimedSeconds };
        return { playback.StartTick, static_cast<double>(playback.OffsetSeconds) };
    }

    double ClipSecondsOver(const AnimPlayback& playback, double ticks, double tickSeconds)
    {
        return ticks * tickSeconds * static_cast<double>(playback.Rate) * static_cast<double>(playback.TimeScale);
    }
}

double AnimElapsedSeconds(const AnimPlayback& playback, AnimTick tick, double tickSeconds)
{
    if (tick <= playback.StartTick)
        return static_cast<double>(playback.OffsetSeconds);
    const Anchor anchor = AnchorOf(playback);
    const auto ticks = static_cast<std::int64_t>(tick - anchor.Tick);
    return anchor.Seconds + ClipSecondsOver(playback, static_cast<double>(ticks), tickSeconds);
}

void RestartAnimPlayback(AnimPlayback& playback, AnimTick start, float offsetSeconds)
{
    playback.StartTick = start;
    playback.OffsetSeconds = offsetSeconds;
    playback.RetimedTick = kAnimNoTick;
    playback.RetimedSeconds = 0.0;
}

void RetimeAnimPlayback(AnimPlayback& playback, AnimTick now, double tickSeconds, float timeScale)
{
    if (playback.TimeScale == timeScale)
        return;
    if (now > playback.StartTick + 1)
    {
        playback.RetimedSeconds = AnimElapsedSeconds(playback, now - 1, tickSeconds);
        playback.RetimedTick = now - 1;
    }
    playback.TimeScale = timeScale;
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
    if (playback.Cyclic || playback.Rate == 0.0f || playback.TimeScale <= 0.0f)
        return kAnimNoTick;
    const Anchor anchor = AnchorOf(playback);
    const double remaining =
        playback.Rate > 0.0f ? static_cast<double>(playback.DurationSeconds) - anchor.Seconds : anchor.Seconds;
    const double perTick = std::abs(ClipSecondsOver(playback, 1.0, tickSeconds));
    // A rounding short of a whole tick still ends on it.
    const double ticks = perTick > 0.0 ? std::ceil(remaining / perTick - 1e-9) : 1.0;
    return std::max(playback.StartTick + 1, anchor.Tick + static_cast<AnimTick>(std::max(ticks, 0.0)));
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
    const double from = carried ? start - ClipSecondsOver(playback, 1.0, tickSeconds) : start;
    if (now <= playback.StartTick + 1)
    {
        add(from, at, !carried, false);
        return crossing;
    }
    add(from, before, !carried, true);
    add(before, at, false, false);
    return crossing;
}
