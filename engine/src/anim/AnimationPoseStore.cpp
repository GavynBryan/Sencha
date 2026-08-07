#include <anim/AnimationPoseStore.h>

#include <cassert>

AnimationPoseHandle AnimationPoseStore::Allocate(uint32_t jointCount)
{
    assert(jointCount > 0 && jointCount <= kMaxSkeletonJoints);

    uint32_t index = UINT32_MAX;
    auto& freeList = m_freeListsByJointCount[jointCount];
    if (!freeList.empty())
    {
        index = freeList.back();
        freeList.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }

    Slot& slot = m_slots[index];
    slot.Occupied = true;
    slot.JointCount = jointCount;
    slot.LocalPose.assign(jointCount, JointTransform{});
    slot.SkinningPalette.assign(jointCount, Mat4::Identity());
    return AnimationPoseHandle{ index, slot.Generation };
}

void AnimationPoseStore::Free(AnimationPoseHandle handle)
{
    if (!Contains(handle))
        return;

    Slot& slot = m_slots[handle.Index];
    const uint32_t jointCount = slot.JointCount;
    slot.Occupied = false;
    slot.JointCount = 0;
    slot.LocalPose.clear();
    slot.SkinningPalette.clear();
    ++slot.Generation;
    if (slot.Generation == 0)
        ++slot.Generation;
    m_freeListsByJointCount[jointCount].push_back(handle.Index);
}

std::span<JointTransform> AnimationPoseStore::LocalPose(AnimationPoseHandle handle)
{
    Slot& slot = CheckedSlot(handle);
    return slot.LocalPose;
}

std::span<const JointTransform> AnimationPoseStore::LocalPose(AnimationPoseHandle handle) const
{
    const Slot& slot = CheckedSlot(handle);
    return slot.LocalPose;
}

std::span<Mat4> AnimationPoseStore::SkinningPalette(AnimationPoseHandle handle)
{
    Slot& slot = CheckedSlot(handle);
    return slot.SkinningPalette;
}

std::span<const Mat4> AnimationPoseStore::SkinningPalette(AnimationPoseHandle handle) const
{
    const Slot& slot = CheckedSlot(handle);
    return slot.SkinningPalette;
}

uint32_t AnimationPoseStore::JointCount(AnimationPoseHandle handle) const
{
    return CheckedSlot(handle).JointCount;
}

bool AnimationPoseStore::Contains(AnimationPoseHandle handle) const
{
    return handle.Index < m_slots.size()
        && m_slots[handle.Index].Occupied
        && m_slots[handle.Index].Generation == handle.Generation;
}

AnimationPoseStore::Slot& AnimationPoseStore::CheckedSlot(AnimationPoseHandle handle)
{
    assert(Contains(handle) && "invalid animation pose handle");
    return m_slots[handle.Index];
}

const AnimationPoseStore::Slot& AnimationPoseStore::CheckedSlot(AnimationPoseHandle handle) const
{
    assert(Contains(handle) && "invalid animation pose handle");
    return m_slots[handle.Index];
}
