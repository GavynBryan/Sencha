#include <render/MeshOverrideSystem.h>

#include <app/GameContexts.h>

void MeshOverrideSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    World& world = ctx.Entities;
    if (!world.IsRegistered<MeshOverrides>() || !world.IsRegistered<StaticMeshComponent>()
        || !world.IsRegistered<SkinnedMeshComponent>())
        return;
    if (LastWorld != &world)
    {
        Static.emplace(world);
        Skinned.emplace(world);
        LastWorld = &world;
    }

    Stale.clear();
    const auto collect = [&](auto& view, auto authoredAt) {
        const auto overrides = view.template Read<MeshOverrides>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            if (overrides[i].ComposedFrom != authoredAt(i))
                Stale.push_back(view.Entity(i));
    };
    Static->ForEachChunk([&](auto& view) {
        const auto meshes = view.template Read<StaticMeshComponent>();
        collect(view, [&](std::uint32_t i) { return meshes[i].Materials; });
    });
    Skinned->ForEachChunk([&](auto& view) {
        const auto meshes = view.template Read<SkinnedMeshComponent>();
        collect(view, [&](std::uint32_t i) { return meshes[i].Materials; });
    });
    for (const EntityId entity : Stale)
        (void)ComposeMeshOverrides(world, entity);
}
