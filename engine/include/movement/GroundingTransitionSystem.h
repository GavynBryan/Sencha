#pragma once

struct FixedLogicContext;
class StoragePartitionSet;
class World;

void RequestGroundingLocomotionModes(World& world);
void RequestGroundingLocomotionModes(
    World& world,
    const StoragePartitionSet& partitions);

class GroundingTransitionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Step(World& world);
};
