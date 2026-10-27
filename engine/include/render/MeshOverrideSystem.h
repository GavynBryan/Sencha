#pragma once

#include <ecs/Query.h>
#include <render/MeshOverrides.h>
#include <render/StaticMeshComponent.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>

#include <optional>
#include <vector>

struct FrameUpdateContext;

// Composes an entity's overrides again when its authored materials change
// under them. Work is proportional to the entities that have overrides.
// Registered only where a window presents.
class MeshOverrideSystem
{
public:
    void FrameUpdate(FrameUpdateContext& ctx);

private:
    const World* LastWorld = nullptr;
    std::optional<Query<Read<MeshOverrides>, Read<StaticMeshComponent>, Without<SkinnedMeshComponent>>> Static;
    std::optional<Query<Read<MeshOverrides>, Read<SkinnedMeshComponent>>> Skinned;
    std::vector<EntityId> Stale;
};
