#include <controller/LookIntegrationSystem.h>

#include <controller/LookOrientation.h>
#include <ecs/Query.h>
#include <input/InputActionState.h>

#include <algorithm>

void LookIntegrationSystem::PreSimulate(PreSimulateContext& ctx)
{
    World& world = ctx.Entities;
    if (!world.IsRegistered<LookOrientation>() || !world.IsRegistered<LocalLookControl>())
        return;

    const LookInputBinding* binding = world.TryGetResource<LookInputBinding>();
    const InputActionState* actions = world.TryGetResource<InputActionState>();
    if (binding == nullptr || actions == nullptr)
        return;

    // The presentation snapshot, not a tick record: a frame that runs no tick
    // still turns the view.
    const Vec2d look = actions->Frame().Axis2(binding->Look);
    if (look.X == 0.0f && look.Y == 0.0f)
        return;

    Query<Write<LookOrientation>, With<LocalLookControl>> query(world);
    query.ForEachChunkIn(ctx.Partitions, [&](auto& view)
    {
        auto orientations = view.template Write<LookOrientation>();
        for (std::uint32_t index = 0; index < view.Count(); ++index)
        {
            LookOrientation& orientation = orientations[index];
            orientation.Yaw -= look.X;
            orientation.Pitch -= look.Y;
            orientation.Pitch =
                std::clamp(orientation.Pitch, orientation.MinPitch, orientation.MaxPitch);
        }
    });
}
