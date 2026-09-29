#pragma once

#include <anim/AnimationClip.h>
#include <anim/Skeleton.h>

#include <string>

// Moves the root joint's planar translation and yaw, measured from the first
// frame, out of the pose and into AnimationClipData::Root; the root keeps its
// height and tilt. Dev-only. Refuses a skeleton with more than one root.
[[nodiscard]] bool ExtractAnimationRootMotion(AnimationClipData& clip, const SkeletonData& skeleton,
                                              std::string* error = nullptr);
