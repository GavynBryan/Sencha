#pragma once

#include <anim/AnimationClip.h>

//=============================================================================
// Root motion sampling
//
// A root curve is data, and content time is a function of a request's start
// tick, so the motion between two content times is the same on every machine
// and on every replay of the same ticks. Movement applies it; nothing here
// moves anything.
//=============================================================================

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

// How far the root moved between two content times, as the character would
// see it: translation in the frame the root faced at `fromSeconds` (x right,
// z back, the model's convention), and the yaw turned. Times are elapsed
// since the content started, not wrapped: cyclic content counts every loop it
// crossed, and one-shot content holds at its end.
struct AnimRootDelta
{
    float X = 0.0f;
    float Z = 0.0f;
    float Yaw = 0.0f;
};

[[nodiscard]] AnimRootDelta AnimRootMotionBetween(const AnimationRootCurve& curve, float durationSeconds,
                                                  double fromSeconds, double toSeconds, bool cyclic);
