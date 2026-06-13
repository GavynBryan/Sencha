#pragma once

#include <math/Mat.h>
#include <math/Quat.h>
#include <math/Vec.h>

#include <cstdint>
#include <string>
#include <vector>

//=============================================================================
// Skeleton (docs/assets/pipeline.md, Decision J)
//
// The shared joint hierarchy: many skinned meshes and many animation clips
// reference one skeleton by AssetRef, with the same refcount composition
// materials→textures use. This is asset-side data only — pose evaluation
// and skinning runtime belong to the animation runtime plan.
//
// Joints are stored parents-before-children (the cook topologically orders
// them), so a single forward pass can compose model-space transforms.
// Inverse-bind matrices are cooked in (Decision N): joint indices in a
// skinned mesh's influence stream are skeleton-local, resolved at cook.
//=============================================================================

// Per-skin joint cap (Decision N): recorded in the skinned .smesh header so
// either skinning runtime can size palettes from the header alone.
inline constexpr uint32_t kMaxSkeletonJoints = 256;

struct SkeletonJoint
{
    // glTF node name; may be empty (names are diagnostics, not identity).
    std::string Name;

    // Index of the parent joint, or -1 for a root. Always less than this
    // joint's own index (topological order is a format invariant).
    int32_t ParentIndex = -1;

    // Bind pose, local to the parent joint.
    Vec3d BindTranslation{};
    Quat<float> BindRotation{};
    Vec3d BindScale = Vec3d(1.0f, 1.0f, 1.0f);

    // Model-space inverse bind matrix, cooked from the source.
    Mat4 InverseBind{};
};

struct SkeletonData
{
    std::vector<SkeletonJoint> Joints;
};

// Format invariants every producer must meet (the runtime never fixes
// data): 1..kMaxSkeletonJoints joints, parents strictly before children,
// finite transforms, unit bind rotations. Errors travel in `error`.
[[nodiscard]] bool ValidateSkeletonData(const SkeletonData& skeleton,
                                        std::string* error = nullptr);
