#include <anim/AnimPosePool.h>

#include <anim/AnimPoseState.h>
#include <anim/AnimationClipSampling.h>
#include <ecs/World.h>

#include <algorithm>
#include <cassert>
#include <utility>

std::uint32_t AnimPosePool::Allocate(EntityId owner)
{
    std::uint32_t index = 0;
    if (!Free.empty())
    {
        index = Free.back();
        Free.pop_back();
    }
    else
    {
        index = static_cast<std::uint32_t>(Slots.size());
        Slots.emplace_back();
    }
    Slot& slot = Slots[index];
    slot = Slot{};
    slot.Owner = owner;
    slot.Live = true;
    return index + 1;
}

void AnimPosePool::Release(std::uint32_t handle, EntityId owner)
{
    Slot* slot = Find(handle, owner);
    if (slot == nullptr)
        return;
    *slot = Slot{};
    Free.push_back(handle - 1);
}

void AnimPosePool::Slot::Shape(std::uint32_t joints, std::uint32_t layers, bool retainLayerPoses)
{
    const std::size_t layerPoseJoints = retainLayerPoses ? static_cast<std::size_t>(joints) * layers : 0;
    if (Joints == joints && Layers == layers && LayerPoses.size() == layerPoseJoints)
        return;
    Joints = joints;
    Layers = layers;
    Offsets.assign(static_cast<std::size_t>(joints) * layers, AnimJointOffset{});
    LayerStates.assign(layers, AnimLayerPose{});
    LayerPoses.assign(layerPoseJoints, Transform3f{});
    Current.assign(joints, Transform3f{});
    Previous.assign(joints, Transform3f{});
    HasCurrent = false;
    HasPrevious = false;
    ++Revision;
}

std::span<const Mat4> AnimPosePool::Slot::PresentedModel(const SkeletonData& skeleton, float alpha) const
{
    assert(skeleton.Joints.size() == Current.size());
    const float blend = std::clamp(alpha, 0.0f, 1.0f);
    if (Presented.Built && Presented.Revision == Revision && Presented.Alpha == blend)
        return Presented.Model;

    // One buffer per thread, the size of the largest rig, not one per entity.
    static thread_local std::vector<Transform3f> local;
    local.resize(Current.size());
    for (std::size_t j = 0; j < Current.size(); ++j)
        local[j] = HasPrevious && blend < 1.0f ? Transform3f::Interpolate(Previous[j], Current[j], blend) : Current[j];
    BuildPosedModelTransforms(skeleton, local, Presented.Model);
    Presented.Revision = Revision;
    Presented.Alpha = blend;
    Presented.Built = true;
    return Presented.Model;
}

AnimPosePool::Slot* AnimPosePool::Find(std::uint32_t handle, EntityId owner)
{
    return const_cast<Slot*>(std::as_const(*this).Find(handle, owner));
}

const AnimPosePool::Slot* AnimPosePool::Find(std::uint32_t handle, EntityId owner) const
{
    if (handle == kAnimNoPoseSlot || handle > Slots.size())
        return nullptr;
    const Slot& slot = Slots[handle - 1];
    return slot.Live && slot.Owner == owner ? &slot : nullptr;
}

void ComponentTraits<AnimPoseState>::OnRemove(const AnimPoseState& state, World& world, EntityId entity)
{
    if (AnimPosePool* pool = world.TryGetResource<AnimPosePool>())
        pool->Release(state.Slot, entity);
}
