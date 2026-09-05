#include "OrbitCameraSystem.h"

#include "PlatformerInputActions.h"

#include <app/GameContexts.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <core/logging/Logger.h>
#include <ecs/World.h>
#include <input/InputActionState.h>
#include <math/Quat.h>
#include <participant/LocalControl.h>
#include <world/RuntimeWorld.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>

void OrbitCameraSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    World& world = ctx.Entities;
    OrbitCameraState* orbit = world.TryGetResource<OrbitCameraState>();
    if (orbit == nullptr)
        orbit = &world.AddResource<OrbitCameraState>();

    // The look action on the presentation clock: the orbit turns at the rate
    // frames arrive, and steering reads the yaw it leaves behind.
    if (const InputActionState* actions = world.TryGetResource<InputActionState>())
    {
        if (const PlatformerInputActions* ids = world.TryGetResource<PlatformerInputActions>())
        {
            const Vec2d look = actions->Frame().Axis2(ids->Look);
            orbit->Yaw -= static_cast<float>(look.X);
            orbit->Pitch = std::clamp(orbit->Pitch - static_cast<float>(look.Y),
                                      orbit->MinPitch, orbit->MaxPitch);
        }
    }

    const EntityId subject = LocalControlSubjectOf(world);
    if (subject != Body)
    {
        Body = subject;
        if (!Camera.IsValid() && Body.IsValid())
        {
            // The game's own camera, in the persistent partition so it outlives
            // whatever level the body is standing in.
            Camera = world.CreateEntity(PersistentStoragePartition);
            world.AddComponent<LocalTransform>(Camera, LocalTransform{});
            SeedDerivedWorldTransform(world, Camera);
            world.AddComponent<CameraComponent>(Camera, CameraComponent{});
        }
        world.GetResource<ActiveCameraService>().SetActive(Body.IsValid() ? Camera : EntityId{});
        if (Body.IsValid() && Log != nullptr)
            Log->Info("PlatformerGame: orbit camera following the player's body");
    }

    if (!Body.IsValid() || !world.IsAlive(Body) || !Camera.IsValid() || !world.IsAlive(Camera))
        return;
    const WorldTransform* target = world.TryGet<WorldTransform>(Body);
    LocalTransform* transform = world.TryGet<LocalTransform>(Camera);
    if (target == nullptr || transform == nullptr)
        return;

    const Vec3d pivot = target->Value.Position + Vec3d{ 0.0f, orbit->PivotHeight, 0.0f };
    const Quatf rotation = Quatf::FromAxisAngle(Vec3d::Up(), orbit->Yaw)
                         * Quatf::FromAxisAngle(Vec3d::Right(), orbit->Pitch);
    transform->Value.Position =
        pivot + rotation.RotateVector(Vec3d::Backward()) * orbit->Distance;
    transform->Value.Rotation = rotation;
}
