#pragma once

#include <ecs/World.h>

class StoragePartitionSet;
struct FixedLogicContext;

//=============================================================================
// Locomotion mode systems
//
// The transition pipeline, in schedule order: requests are collected from the
// per-mode request tags, arbitrated and applied atomically, and the resulting
// physical facts are projected back onto the tag container.
//
// Each system exposes a whole-world Step for tests; the scheduled path visits
// only the partitions participating this tick.
//=============================================================================

// Turns a granted movement.request.* tag into a mode transition request and
// consumes the tag.
class ModeRequestCollectionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Step(World& world);

private:
    void StepImpl(World& world, const StoragePartitionSet* partitions);
};

// Applies at most one transition per entity per tick: exit the old session,
// enter the new one, set the mode. Rejected requests discard whatever
// candidate the target mode staged.
class LocomotionModeTransitionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Step(World& world);

private:
    void StepImpl(World& world, const StoragePartitionSet* partitions);
};

class ModeTagProjectionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Step(World& world);

private:
    void StepImpl(World& world, const StoragePartitionSet* partitions);
};

class SupportTagProjectionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Step(World& world);

private:
    void StepImpl(World& world, const StoragePartitionSet* partitions);
};
