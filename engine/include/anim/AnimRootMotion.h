#pragma once

#include <anim/AnimationClip.h>

// Where the root is at one time: planar offset and yaw from the clip's start,
// in the skeleton's model space.
struct AnimRootPose
{
    float X = 0.0f;
    float Z = 0.0f;
    float Yaw = 0.0f;
};

// Linear between keys, held before the first and after the last.
[[nodiscard]] AnimRootPose SampleAnimRootCurve(const AnimationRootCurve& curve, float seconds);

// Translation in the frame the root faced at `fromSeconds` (x right, z back) and yaw
// turned. Times are unwrapped: cyclic content counts every loop, one-shot holds at its end.
struct AnimRootDelta
{
    float X = 0.0f;
    float Z = 0.0f;
    float Yaw = 0.0f;
};

[[nodiscard]] AnimRootDelta AnimRootMotionBetween(const AnimationRootCurve& curve, float durationSeconds,
                                                  double fromSeconds, double toSeconds, bool cyclic);
