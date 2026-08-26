#pragma once

#include <anim/AnimationClip.h>
#include <anim/Skeleton.h>
#include <math/geometry/3d/Transform3d.h>

#include <span>
#include <vector>

//=============================================================================
// Animation clip sampling
//
// A clip and a time in, one local TRS per joint out -- the missing middle
// between the cooked clip and the skinning palette. Pure over plain values,
// so a pose is testable without an entity, a device, or a frame.
//
// Every joint starts at its bind TRS and the clip's tracks overwrite what
// they animate: tracks are sparse per (joint, channel), so a clip that only
// rotates one joint leaves everything else exactly at bind -- which is what
// keeps an unanimated skeleton byte-identical to the rest pose.
//
// The sampler defends nothing about the data. Load-time validation already
// guarantees ascending finite key times, value counts matching key counts,
// unit rotation keys, and joint indices inside the skeleton, so re-checking
// here would be dead branches in a per-frame path.
//=============================================================================

// Fills `out` with one local-to-parent transform per skeleton joint,
// resized to the joint count. Times outside [0, last key] clamp to the
// nearest key; Step holds the preceding key, Linear interpolates (rotations
// by shortest-path slerp).
void SampleAnimationClip(const AnimationClipData& clip,
                         const SkeletonData& skeleton,
                         float timeSeconds,
                         std::vector<Transform3f>& out);

// Model-space transforms composed parent-first from posed local transforms,
// the posed twin of BuildBindModelTransforms. Feeds BuildSkinningPalette.
void BuildPosedModelTransforms(const SkeletonData& skeleton,
                               std::span<const Transform3f> localPose,
                               std::vector<Mat4>& out);
