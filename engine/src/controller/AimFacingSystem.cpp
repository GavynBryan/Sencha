#include <controller/AimFacingSystem.h>

#include <controller/LookOrientation.h>
#include <ecs/Query.h>
#include <math/Quat.h>
#include <world/transform/TransformComponents.h>

void AimFacingSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    if (!world.IsRegistered<LookOrientation>() || !world.IsRegistered<AimFacing>()
        || !world.IsRegistered<LocalTransform>())
    {
        return;
    }

    Query<Write<LocalTransform>, Read<LookOrientation>, With<AimFacing>> query(world);
    query.ForEachChunkIn(ctx.Partitions, [](auto& view)
    {
        auto transforms = view.template Write<LocalTransform>();
        auto orientations = view.template Read<LookOrientation>();
        for (std::uint32_t index = 0; index < view.Count(); ++index)
        {
            // The raw running total, not a wrapped one: this is the angle the
            // camera builds its own pose from, and a body that folded it into a
            // circle would face a hair away from the view it shares an aim with
            // wherever the two conventions disagreed.
            transforms[index].Value.Rotation =
                Quatf::FromAxisAngle(Vec3d::Up(), orientations[index].Yaw);
        }
    });
}
