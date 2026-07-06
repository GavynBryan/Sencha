#pragma once

#include <anim/Skeleton.h>
#include <anim/SkeletonPose.h>
#include <math/Mat.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

struct AnimationPoseHandle
{
    uint32_t Index = UINT32_MAX;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != UINT32_MAX; }
};

class AnimationPoseStore
{
public:
    [[nodiscard]] AnimationPoseHandle Allocate(uint32_t jointCount);
    void Free(AnimationPoseHandle handle);

    [[nodiscard]] std::span<JointTransform> LocalPose(AnimationPoseHandle handle);
    [[nodiscard]] std::span<const JointTransform> LocalPose(AnimationPoseHandle handle) const;
    [[nodiscard]] std::span<Mat4> SkinningPalette(AnimationPoseHandle handle);
    [[nodiscard]] std::span<const Mat4> SkinningPalette(AnimationPoseHandle handle) const;
    [[nodiscard]] uint32_t JointCount(AnimationPoseHandle handle) const;
    [[nodiscard]] bool Contains(AnimationPoseHandle handle) const;

private:
    struct Slot
    {
        uint32_t Generation = 1;
        uint32_t JointCount = 0;
        bool Occupied = false;
        std::vector<JointTransform> LocalPose;
        std::vector<Mat4> SkinningPalette;
    };

    [[nodiscard]] Slot& CheckedSlot(AnimationPoseHandle handle);
    [[nodiscard]] const Slot& CheckedSlot(AnimationPoseHandle handle) const;

    std::vector<Slot> m_slots;
    std::array<std::vector<uint32_t>, kMaxSkeletonJoints + 1> m_freeListsByJointCount;
};
