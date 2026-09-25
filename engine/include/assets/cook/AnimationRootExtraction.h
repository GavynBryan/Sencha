#pragma once

#include <anim/AnimationClip.h>
#include <anim/Skeleton.h>

#include <string>

//=============================================================================
// Root motion extraction. Dev-only, compiled under SENCHA_ENABLE_COOK.
//
// Moves a clip's travel across the ground out of its pose and into
// AnimationClipData::Root: the skeleton's root joint keeps its height and its
// tilt, and loses its planar translation and its yaw, both measured from the
// clip's first frame. What the root keeps is what the pose shows in place;
// what it gives up is what the movement pipeline applies to the character.
//
// Opt-in per clip from the source's import sidecar: extracting changes how the
// clip poses when it plays without root motion, so it is never a default.
//=============================================================================

// Refuses a skeleton with more than one root: nothing says which one carries
// the character.
[[nodiscard]] bool ExtractAnimationRootMotion(AnimationClipData& clip, const SkeletonData& skeleton,
                                              std::string* error = nullptr);
