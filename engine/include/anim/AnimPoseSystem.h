#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimPoseEvaluation.h>
#include <anim/AnimPoseState.h>
#include <anim/AnimRig.h>
#include <ecs/Query.h>

#include <cstddef>
#include <optional>
#include <vector>

class JobSystem;
class StoragePartitionSet;
class World;
struct PostFixedContext;

//=============================================================================
// AnimPoseSystem
//
// Poses every rigged entity once per fixed tick, after movement has moved it,
// in a World that presents a pose. A rig whose skeleton is loaded gets pose
// state and a pool slot on its first pass; each pass then evaluates every such
// entity into its slot.
//
// The pass has two halves. On the owner thread it assigns and shapes slots and
// resolves each entity's binding, which is the only work that touches shared
// state. Then it evaluates entities, each touching only its own components and
// slot, across the job system -- or inline in entity order with no workers,
// which is the reference every parallel pass must match.
//=============================================================================
class AnimPoseSystem
{
public:
    // `jobs` may be null: every entity is then posed inline.
    explicit AnimPoseSystem(JobSystem* jobs = nullptr);
    ~AnimPoseSystem();

    AnimPoseSystem(const AnimPoseSystem&) = delete;
    AnimPoseSystem& operator=(const AnimPoseSystem&) = delete;

    void PostFixed(PostFixedContext& ctx);
    void Pose(World& world, AnimTick now, double tickSeconds);

    // Entities posed on the last pass.
    [[nodiscard]] std::size_t Posed() const { return Items.size(); }

private:
    void PoseImpl(World& world, const StoragePartitionSet* partitions, AnimTick now, double tickSeconds);

    struct Item
    {
        AnimPoseInput Input;
        // Resolved to Input.Slot once every slot is assigned: assigning one
        // can move the others.
        std::uint32_t SlotHandle = 0;
    };

    JobSystem* Jobs = nullptr;
    const World* LastWorld = nullptr;
    std::optional<Query<Read<AnimRig>, Without<AnimPoseState>>> Unposed;
    std::optional<Query<Read<AnimRig>, Read<AnimContentState>, Write<AnimPoseState>>> Posing;
    std::vector<Item> Items;
    std::vector<AnimPoseScratch> Scratch;
};
