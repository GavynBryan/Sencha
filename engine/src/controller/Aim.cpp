#include <controller/Aim.h>

#include <controller/AimOrigin.h>
#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <math/Quat.h>
#include <world/transform/TransformComponents.h>

Aim AimOf(const World& world, EntityId entity)
{
    Aim aim;
    if (!world.IsAlive(entity))
        return aim;
    const WorldTransform* placed = world.IsRegistered<WorldTransform>() ? world.TryGet<WorldTransform>(entity) : nullptr;
    const LocalTransform* local = world.IsRegistered<LocalTransform>() ? world.TryGet<LocalTransform>(entity) : nullptr;
    const Transform3f* transform = placed != nullptr ? &placed->Value : local != nullptr ? &local->Value : nullptr;
    if (transform == nullptr)
        return aim;

    const LookOrientation* look = world.IsRegistered<LookOrientation>() ? world.TryGet<LookOrientation>(entity) : nullptr;
    const Quatf facing = look != nullptr ? Quatf::FromPitchYawRoll(0.0f, look->Yaw, 0.0f) : transform->Rotation;
    const Quatf looking = look != nullptr ? Quatf::FromPitchYawRoll(look->Pitch, look->Yaw, 0.0f) : transform->Rotation;
    const AimOrigin* origin = world.IsRegistered<AimOrigin>() ? world.TryGet<AimOrigin>(entity) : nullptr;
    aim.Origin = transform->Position + (origin != nullptr ? facing.RotateVector(origin->Offset) : Vec3d::Zero());
    aim.Direction = looking.RotateVector(Vec3d::Forward());
    return aim;
}
