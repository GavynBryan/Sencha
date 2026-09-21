#include "AnimationClipPreviewSession.h"

#include <anim/AnimationClipSampling.h>
#include <anim/SkinningPalette.h>
#include <core/assets/AssetPath.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

bool AnimationClipPreviewSession::SetContent(std::string skeletonPath,
                                            SkeletonData skeleton,
                                            std::optional<AnimationClipData> clip,
                                            std::string& error)
{
    error.clear();
    if (!IsValidAssetPath(skeletonPath))
    {
        error = "Preview skeleton must have an asset:// path.";
        return false;
    }
    if (!ValidateSkeletonData(skeleton, &error))
        return false;
    if (clip)
    {
        if (!ValidateAnimationClipData(*clip, &error))
            return false;
        if (clip->SkeletonPath != skeletonPath)
        {
            error = "Clip and preview mesh reference different skeletons; retargeting is not implicit.";
            return false;
        }
        for (const auto& track : clip->Tracks)
            if (track.JointIndex >= skeleton.Joints.size())
            {
                error = "Clip contains a joint outside the preview skeleton.";
                return false;
            }
        // Keep tick conversion and advancement defined for otherwise valid
        // imported durations. Leave headroom for the final frame's increment.
        constexpr auto maxTick = std::numeric_limits<std::uint64_t>::max() / 2;
        if (static_cast<double>(clip->DurationSeconds) * TickRate > static_cast<double>(maxTick))
        {
            error = "Clip duration exceeds the preview clock's tick range.";
            return false;
        }
    }
    Path = std::move(skeletonPath);
    SkeletonValue = std::move(skeleton);
    Clip = std::move(clip);
    Restart();
    Pause();
    return true;
}

void AnimationClipPreviewSession::Clear()
{
    Path.clear();
    SkeletonValue.Joints.clear();
    Clip.reset();
    Restart();
    Pause();
}

void AnimationClipPreviewSession::Play()
{
    ReturnToPlayback();
    if (Duration() > 0.0)
    {
        if (PlaybackTick >= EndTick())
            Restart();
        Playing = true;
    }
}

void AnimationClipPreviewSession::Pause()
{
    Playing = false;
    AccumulatedTicks = 0.0;
}

void AnimationClipPreviewSession::Restart()
{
    PlaybackTick = 0;
    AccumulatedTicks = 0.0;
    InspectionSeconds.reset();
    PoseDirty = true;
}

void AnimationClipPreviewSession::Step(int direction)
{
    Pause();
    ReturnToPlayback();
    if (direction > 0 && PlaybackTick < EndTick())
        ++PlaybackTick;
    else if (direction < 0 && PlaybackTick > 0)
        --PlaybackTick;
    PoseDirty = true;
}

void AnimationClipPreviewSession::Advance(double wallSeconds)
{
    if (!Playing || !std::isfinite(wallSeconds) || wallSeconds <= 0.0)
        return;
    // A stalled editor frame contributes at most a quarter second. Audition
    // has no simulation debt, events, or gameplay effects to discard.
    AccumulatedTicks += std::min(wallSeconds, 0.25) * PlaybackSpeed * TickRate;
    const auto ticks = static_cast<std::uint64_t>(std::floor(AccumulatedTicks + 1e-9));
    AccumulatedTicks = std::max(0.0, AccumulatedTicks - static_cast<double>(ticks));
    if (ticks == 0)
        return;
    PlaybackTick += ticks;
    const auto end = EndTick();
    if (PlaybackTick >= end)
    {
        if (Loop && end != 0)
            PlaybackTick %= end;
        else
        {
            PlaybackTick = end;
            Pause();
        }
    }
    PoseDirty = true;
}

void AnimationClipPreviewSession::InspectNormalized(double normalizedTime)
{
    if (!std::isfinite(normalizedTime))
        return;
    Pause();
    InspectionSeconds = std::clamp(normalizedTime, 0.0, 1.0) * Duration();
    PoseDirty = true;
}

void AnimationClipPreviewSession::ReturnToPlayback()
{
    InspectionSeconds.reset();
    PoseDirty = true;
}

bool AnimationClipPreviewSession::SetSpeed(double speed)
{
    if (!std::isfinite(speed) || speed <= 0.0 || speed > 8.0)
        return false;
    PlaybackSpeed = speed;
    return true;
}

double AnimationClipPreviewSession::Duration() const
{
    return Clip ? static_cast<double>(Clip->DurationSeconds) : 0.0;
}

std::uint64_t AnimationClipPreviewSession::EndTick() const
{
    // Non-integral durations end on the first tick at or beyond the last key.
    return static_cast<std::uint64_t>(std::ceil(Duration() * TickRate));
}

double AnimationClipPreviewSession::SampleSeconds() const
{
    return InspectionSeconds.value_or(
        std::min(static_cast<double>(PlaybackTick) / TickRate, Duration()));
}

double AnimationClipPreviewSession::NormalizedTime() const
{
    return Duration() > 0.0 ? SampleSeconds() / Duration() : 0.0;
}

const std::vector<Mat4>& AnimationClipPreviewSession::Palette()
{
    if (PoseDirty)
        Sample();
    return SkinPalette;
}

void AnimationClipPreviewSession::Sample()
{
    if (Clip)
    {
        SampleAnimationClip(*Clip, SkeletonValue, static_cast<float>(SampleSeconds()), LocalPose);
        BuildPosedModelTransforms(SkeletonValue, LocalPose, ModelPose);
    }
    else
        BuildBindModelTransforms(SkeletonValue, ModelPose);
    BuildSkinningPalette(SkeletonValue, ModelPose, SkinPalette);
    PoseDirty = false;
}
