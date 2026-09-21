#pragma once

#include <anim/AnimationClip.h>
#include <anim/Skeleton.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Isolated content audition, not gameplay simulation. Arbitrary-time inspection
// never changes the playback clock. Captured asset values survive cache reloads.
class AnimationClipPreviewSession
{
public:
    [[nodiscard]] bool SetContent(std::string skeletonPath, SkeletonData skeleton,
                                  std::optional<AnimationClipData> clip, std::string& error);
    void Clear();
    void Play();
    void Pause();
    void Restart();
    void Step(int direction);
    void Advance(double wallSeconds);
    void InspectNormalized(double normalizedTime);
    void ReturnToPlayback();
    [[nodiscard]] bool SetSpeed(double speed);
    void SetLoop(bool loop) { Loop = loop; }

    [[nodiscard]] bool IsPlaying() const { return Playing; }
    [[nodiscard]] bool IsLooping() const { return Loop; }
    [[nodiscard]] bool IsInspecting() const { return InspectionSeconds.has_value(); }
    [[nodiscard]] double Speed() const { return PlaybackSpeed; }
    [[nodiscard]] std::uint64_t Tick() const { return PlaybackTick; }
    [[nodiscard]] double Duration() const;
    [[nodiscard]] double SampleSeconds() const;
    [[nodiscard]] double NormalizedTime() const;
    [[nodiscard]] const std::string& SkeletonPath() const { return Path; }
    [[nodiscard]] const SkeletonData& Skeleton() const { return SkeletonValue; }
    [[nodiscard]] const std::vector<Mat4>& Palette();
    static constexpr std::uint32_t TickRate = 60;

private:
    [[nodiscard]] std::uint64_t EndTick() const;
    void Sample();
    std::string Path;
    SkeletonData SkeletonValue;
    std::optional<AnimationClipData> Clip;
    std::vector<Transform3f> LocalPose;
    std::vector<Mat4> ModelPose;
    std::vector<Mat4> SkinPalette;
    std::uint64_t PlaybackTick = 0;
    double AccumulatedTicks = 0.0;
    double PlaybackSpeed = 1.0;
    std::optional<double> InspectionSeconds;
    bool Playing = false;
    bool Loop = true;
    bool PoseDirty = true;
};
