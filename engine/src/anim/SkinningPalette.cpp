#include <anim/SkinningPalette.h>

#include <math/geometry/3d/Transform3d.h>

#include <cassert>

void BuildBindModelTransforms(const SkeletonData& skeleton, std::vector<Mat4>& out)
{
    out.clear();
    out.reserve(skeleton.Joints.size());
    for (const SkeletonJoint& joint : skeleton.Joints)
    {
        Transform3f local;
        local.Position = joint.BindTranslation;
        local.Rotation = joint.BindRotation;
        local.Scale = joint.BindScale;

        const Mat4 localMatrix = local.ToMat4();
        if (joint.ParentIndex < 0)
        {
            out.push_back(localMatrix);
            continue;
        }
        // Parents strictly precede children (format invariant), so the
        // parent's model transform is already in `out`.
        assert(static_cast<size_t>(joint.ParentIndex) < out.size());
        out.push_back(out[static_cast<size_t>(joint.ParentIndex)] * localMatrix);
    }
}

void BuildSkinningPalette(const SkeletonData& skeleton,
                          std::span<const Mat4> modelTransforms,
                          std::vector<Mat4>& out)
{
    assert(modelTransforms.size() == skeleton.Joints.size());
    out.clear();
    out.reserve(skeleton.Joints.size());
    for (size_t joint = 0; joint < skeleton.Joints.size(); ++joint)
        out.push_back(modelTransforms[joint] * skeleton.Joints[joint].InverseBind);
}
