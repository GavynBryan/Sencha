#include <anim/Skeleton.h>

#include <cmath>
#include <format>

namespace
{
    bool Fail(std::string* error, std::string message)
    {
        if (error)
            *error = std::move(message);
        return false;
    }

    bool IsFinite(const Vec3d& v)
    {
        return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
    }

    bool IsFinite(const Quat<float>& q)
    {
        return std::isfinite(q.X) && std::isfinite(q.Y) && std::isfinite(q.Z)
            && std::isfinite(q.W);
    }

    bool IsFinite(const Mat4& m)
    {
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                if (!std::isfinite(m.Data[row][col]))
                    return false;
        return true;
    }
} // namespace

bool ValidateSkeletonData(const SkeletonData& skeleton, std::string* error)
{
    if (skeleton.Joints.empty())
        return Fail(error, "skeleton has no joints");
    if (skeleton.Joints.size() > kMaxSkeletonJoints)
        return Fail(error, std::format("skeleton has {} joints (cap is {})",
                                       skeleton.Joints.size(), kMaxSkeletonJoints));

    for (size_t i = 0; i < skeleton.Joints.size(); ++i)
    {
        const SkeletonJoint& joint = skeleton.Joints[i];

        if (joint.ParentIndex < -1 || joint.ParentIndex >= static_cast<int32_t>(i))
            return Fail(error, std::format(
                "joint {} has parent {}; parents must precede children (-1 for roots)",
                i, joint.ParentIndex));

        if (!IsFinite(joint.BindTranslation) || !IsFinite(joint.BindRotation)
            || !IsFinite(joint.BindScale) || !IsFinite(joint.InverseBind))
            return Fail(error, std::format("joint {} has a non-finite transform", i));

        const float lengthSq = joint.BindRotation.Dot(joint.BindRotation);
        if (std::abs(lengthSq - 1.0f) > 1e-3f)
            return Fail(error, std::format("joint {} bind rotation is not unit length", i));
    }

    return true;
}
