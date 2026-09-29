#pragma once

#include <anim/AnimTypes.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// One clip instance on the tick clock. Content, flows, events, poses and root motion
// all ask it where the clip is at a tick; see docs/gameplay/animation.md, "Playback time".
struct AnimPlayback
{
    AnimTick StartTick = 0;
    // Clip seconds at StartTick.
    float OffsetSeconds = 0.0f;
    // Clip seconds per second of ticks; negative plays backwards and zero holds.
    float Rate = 1.0f;
    float DurationSeconds = 0.0f;
    // Wraps at its ends rather than holding them.
    bool Cyclic = false;

    friend bool operator==(const AnimPlayback&, const AnimPlayback&) = default;
};

// Clip seconds at `tick` before placing; a tick before the start reads the offset.
[[nodiscard]] double AnimElapsedSeconds(const AnimPlayback& playback, AnimTick tick, double tickSeconds);

// `elapsed` on a clip of `duration`: wrapped either way round when cyclic, held at the
// ends otherwise.
[[nodiscard]] double AnimPlaceSeconds(double elapsed, double duration, bool cyclic);

[[nodiscard]] double AnimPlaybackSeconds(const AnimPlayback& playback, AnimTick tick, double tickSeconds);

// The first tick at or past the end: forward at its length, backward at its start, and
// never the tick it began, so a sequence always moves forward in time. kAnimNoTick for
// cyclic or held playback.
[[nodiscard]] AnimTick AnimPlaybackEndTick(const AnimPlayback& playback, double tickSeconds);
[[nodiscard]] bool AnimPlaybackEnded(const AnimPlayback& playback, AnimTick tick, double tickSeconds);

// How a pass meets an instance it has not crossed before.
enum class AnimPlaybackEntry : std::uint8_t
{
    // It begins at its offset, so a mark there is crossed.
    Start,
    // It carries on another instance's phase, so the tick leading up to it is its own.
    Carried,
};

// A stretch of one instance's unplaced clip time. Marks in a Skipped stretch were
// passed over by a jump, not played, and are recorded rather than produced.
struct AnimCrossedStretch
{
    double From = 0.0;
    double To = 0.0;
    bool IncludeFrom = false;
    bool Skipped = false;
};

struct AnimCrossing
{
    AnimCrossedStretch Stretches[2] = {};
    std::uint8_t Count = 0;
};

// What a pass at `now` crosses of `playback`, already crossed through `through`
// (kAnimNoTick on entry). Only the tick before `now` plays; see
// docs/gameplay/animation.md, "Playback time", for what is skipped.
[[nodiscard]] AnimCrossing AnimCrossPlayback(const AnimPlayback& playback, AnimTick through,
                                             AnimPlaybackEntry entry, AnimTick now, double tickSeconds);

// Content shorter than a tick loops several times per tick; past this many loops, its
// marks cannot all be meant.
inline constexpr std::int64_t kAnimMaxLoopsPerStretch = 64;

// Every mark of a clip inside `stretch`, in the order the clip passes them: `markSeconds(i)`
// is mark i's time for i in [0, marks), and `visit(i)` is called per crossing. A cyclic clip
// repeats its marks each loop; a skipped stretch visits each mark at most once.
template <typename MarkSeconds, typename Visit>
void ForEachAnimMark(const AnimPlayback& playback, const AnimCrossedStretch& stretch, std::size_t marks,
                     MarkSeconds&& markSeconds, Visit&& visit)
{
    if (marks == 0)
        return;
    const double duration = static_cast<double>(playback.DurationSeconds);
    const bool backward = stretch.To < stretch.From;
    // Forward stretches cross (from, to]; backward ones [to, from), nearest first.
    double low = backward ? stretch.To : stretch.From;
    const double high = backward ? stretch.From : stretch.To;
    const bool includeLow = backward || stretch.IncludeFrom;
    const bool includeHigh = !backward || stretch.IncludeFrom;
    const auto inside = [&](double at) {
        return (at > low || (includeLow && at == low)) && (at < high || (includeHigh && at == high));
    };
    if (!playback.Cyclic || duration <= 0.0)
    {
        for (std::size_t k = 0; k < marks; ++k)
        {
            const std::size_t e = backward ? marks - 1 - k : k;
            if (inside(markSeconds(e)))
                visit(e);
        }
        return;
    }
    if (stretch.Skipped && high - low > duration)
        low = high - duration;
    const auto firstLoop = static_cast<std::int64_t>(std::floor(low / duration));
    const auto lastLoop =
        std::min(static_cast<std::int64_t>(std::floor(high / duration)), firstLoop + kAnimMaxLoopsPerStretch);
    for (std::int64_t n = 0; n <= lastLoop - firstLoop; ++n)
    {
        const std::int64_t loop = backward ? lastLoop - n : firstLoop + n;
        for (std::size_t k = 0; k < marks; ++k)
        {
            const std::size_t e = backward ? marks - 1 - k : k;
            if (inside(static_cast<double>(loop) * duration + markSeconds(e)))
                visit(e);
        }
    }
}
