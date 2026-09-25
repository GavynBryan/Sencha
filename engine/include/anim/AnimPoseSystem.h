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

// Runs after movement in a World that presents a pose. Slots are assigned on the
// owner thread; evaluation then runs per entity across jobs, or inline in entity
// order without workers, which is the reference the parallel path must match.
class AnimPoseSystem
{
public:
    explicit AnimPoseSystem(JobSystem* jobs = nullptr);
    ~AnimPoseSystem();

    AnimPoseSystem(const AnimPoseSystem&) = delete;
    AnimPoseSystem& operator=(const AnimPoseSystem&) = delete;

    void PostFixed(PostFixedContext& ctx);
    void Pose(World& world, AnimTick now, double tickSeconds);

    [[nodiscard]] std::size_t Posed() const { return Items.size(); }

private:
    void PoseImpl(World& world, const StoragePartitionSet* partitions, AnimTick now, double tickSeconds);

    struct Item
    {
        AnimPoseInput Input;
        // Resolved to Input.Slot after every slot is assigned, since assigning moves slots.
        std::uint32_t SlotHandle = 0;
    };

    JobSystem* Jobs = nullptr;
    const World* LastWorld = nullptr;
    std::optional<Query<Read<AnimRig>, Without<AnimPoseState>>> Unposed;
    std::optional<Query<Read<AnimRig>, Read<AnimContentState>, Write<AnimPoseState>>> Posing;
    std::vector<Item> Items;
    std::vector<AnimPoseScratch> Scratch;
};
