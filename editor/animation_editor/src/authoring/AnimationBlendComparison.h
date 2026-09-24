#pragma once

#include "authoring/AnimationPreviewSession.h"

#include <anim/AnimTypes.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

//=============================================================================
// Blend comparison
//
// An A/B of two runs of one scenario: a take records the subject's composed
// pose on every tick the preview kept, with the scenario that produced it, and
// two takes compare tick by tick. Only takes of the same scenario compare --
// the same inputs, requests, tick rate and seed from the same start -- so a
// residual is what the edits between them did and nothing else.
//=============================================================================

struct AnimationPoseTake
{
    std::string Label;
    // The scenario as written when the take ran.
    std::string ScenarioForm;
    std::vector<AnimTick> Ticks;
    std::vector<std::vector<Transform3f>> Poses;

    // The pose on `tick`, or null when the take has none then.
    [[nodiscard]] const std::vector<Transform3f>* At(AnimTick tick) const;
};

// The session's history as a take.
[[nodiscard]] AnimationPoseTake RecordAnimationPoseTake(const AnimationPreviewSession& session, std::string label);

// How far one tick's pose in B is from A's: the largest joint distance and
// rotation, and the joint each was largest at.
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
    // Empty when the takes compare; otherwise why they do not.
    std::string Refusal;
    // One per tick both takes have, in tick order.
    std::vector<AnimationPoseResidual> Residuals;
};

[[nodiscard]] AnimationPoseComparison CompareAnimationPoseTakes(const AnimationPoseTake& a, const AnimationPoseTake& b);
