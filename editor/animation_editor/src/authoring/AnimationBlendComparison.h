#pragma once

#include "authoring/AnimationPreviewSession.h"

#include <anim/AnimTypes.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct AnimationPoseTake
{
    std::string Label;
    // Serialized scenario; takes compare only when these match.
    std::string ScenarioText;
    std::vector<AnimTick> Ticks;
    std::vector<std::vector<Transform3f>> Poses;

    [[nodiscard]] const std::vector<Transform3f>* At(AnimTick tick) const;
};

[[nodiscard]] AnimationPoseTake RecordAnimationPoseTake(const AnimationPreviewSession& session, std::string label);

// Largest per-joint position and rotation difference, and where each occurred.
struct AnimationPoseResidual
{
    AnimTick Tick = 0;
    float Position = 0.0f;
    float Rotation = 0.0f;
    std::uint32_t PositionJoint = 0;
    std::uint32_t RotationJoint = 0;
};

[[nodiscard]] AnimationPoseResidual AnimationPoseDifference(AnimTick tick, const std::vector<Transform3f>& a,
                                                            const std::vector<Transform3f>& b);

struct AnimationPoseComparison
{
    std::string Refusal;
    // Ticks both takes have, in order.
    std::vector<AnimationPoseResidual> Residuals;
};

[[nodiscard]] AnimationPoseComparison CompareAnimationPoseTakes(const AnimationPoseTake& a, const AnimationPoseTake& b);

// Take A recorded from the simulation, and how a replay of the working scenario compares with it.
class AnimationTakeComparison
{
public:
    bool RecordA(const AnimationPreviewSession& simulation);
    // Replays from tick 0 to take A's last tick, so any edit since the recording shows as a residual.
    bool ReplayAgainstA(AnimationPreviewSession& simulation);
    void Clear();
    [[nodiscard]] const AnimationPoseTake* A() const { return TakeA ? &*TakeA : nullptr; }
    [[nodiscard]] const AnimationPoseComparison& Comparison() const { return Result; }

private:
    std::optional<AnimationPoseTake> TakeA;
    AnimationPoseComparison Result;
};
