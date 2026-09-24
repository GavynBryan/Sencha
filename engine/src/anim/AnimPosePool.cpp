#include <anim/AnimPosePool.h>

#include <anim/AnimPoseState.h>
#include <ecs/World.h>

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

void AnimPosePool::Release(std::uint32_t handle)
{
    Slot* slot = Find(handle);
    if (slot == nullptr)
        return;
    *slot = Slot{};
    Free.push_back(handle - 1);
}

void AnimPosePool::Shape(std::uint32_t handle, std::uint32_t joints, std::uint32_t layers)
{
    Slot* slot = Find(handle);
    if (slot == nullptr || (slot->Joints == joints && slot->Layers == layers))
        return;
    slot->Joints = joints;
    slot->Layers = layers;
    slot->Offsets.assign(static_cast<std::size_t>(joints) * layers, AnimJointOffset{});
    slot->LayerPoses.assign(static_cast<std::size_t>(joints) * layers, Transform3f{});
    slot->Current.assign(joints, Transform3f{});
    slot->Previous.assign(joints, Transform3f{});
    slot->HasCurrent = false;
    slot->HasPrevious = false;
}

AnimPosePool::Slot* AnimPosePool::Find(std::uint32_t handle)
{
    if (handle == kAnimNoPoseSlot || handle > Slots.size() || !Slots[handle - 1].Live)
        return nullptr;
    return &Slots[handle - 1];
}

const AnimPosePool::Slot* AnimPosePool::Find(std::uint32_t handle) const
{
    if (handle == kAnimNoPoseSlot || handle > Slots.size() || !Slots[handle - 1].Live)
        return nullptr;
    return &Slots[handle - 1];
}

void ComponentTraits<AnimPoseState>::OnRemove(const AnimPoseState& state, World& world, EntityId)
{
    if (AnimPosePool* pool = world.TryGetResource<AnimPosePool>())
        pool->Release(state.Slot);
}
