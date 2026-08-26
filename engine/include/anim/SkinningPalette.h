#pragma once

#include <anim/Skeleton.h>
#include <math/Mat.h>

#include <span>
#include <vector>

//=============================================================================
// Skinning palette
//
// The matrices a skinned vertex is transformed by: per joint, the joint's
// model-space transform times its cooked inverse bind. The pose source is the
// animation runtime's business; these are the pure halves both skinning
// branches (vertex-shader and compute pre-skin) consume identically, kept
// beside the skeleton so they are testable without either existing.
//
// The identity these encode -- and the test that pins it -- is the contract
// between the cook's InverseBind and the bind TRS it was derived from: at bind
// pose, every palette entry is the identity matrix, which is why a rest-pose
// skinned mesh can draw through the static path byte-exactly.
//=============================================================================

// Model-space transforms of every joint at bind pose, composed parent-first.
// The parents-before-children joint order is a format invariant the loader
// validated, so one forward walk is complete.
void BuildBindModelTransforms(const SkeletonData& skeleton, std::vector<Mat4>& out);

// out[j] = modelTransforms[j] * InverseBind[j]. Sizes must match the skeleton.
void BuildSkinningPalette(const SkeletonData& skeleton,
                          std::span<const Mat4> modelTransforms,
                          std::vector<Mat4>& out);
