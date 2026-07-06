#pragma once

#include <anim/AnimationClip.h>
#include <anim/Skeleton.h>
#include <anim/SkeletonPose.h>

#include <span>

// Deterministic sampler rules: step selects key i where Times[i] <= t < Times[i + 1].
// Linear interpolation lerps translation and scale, and nlerps rotation after a
// hemisphere fix. Callers pre-fill pose from bind if untracked channels should bind.
[[nodiscard]] float WrapClipTime(float timeSeconds, float durationSeconds, bool looping);
void WriteBindPose(const SkeletonData& skeleton, std::span<JointTransform> pose);
void SampleClip(const AnimationClipData& clip,
                float timeSeconds,
                std::span<JointTransform> pose);
