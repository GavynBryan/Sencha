#include <camera/CameraFollowSystem.h>

#include <app/GameContexts.h>
#include <components/ActiveCameraService.h>
#include <ecs/World.h>
#include <camera/CameraRig.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>

void CameraFollowSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    for (Registry* reg : ctx.ActiveRegistries)
    {
        World& world = reg->Components;
        if (!world.IsRegistered<CameraRig>())
            continue;
        const ActiveCameraService* cameraService = reg->Resources.TryGet<ActiveCameraService>();
        if (cameraService == nullptr || !cameraService->HasActive())
            continue;

        const EntityId cameraEntity = cameraService->GetActive();
        CameraRig* rig = world.TryGet<CameraRig>(cameraEntity);
        LocalTransform* cameraTransform = world.TryGet<LocalTransform>(cameraEntity);
        if (rig == nullptr || cameraTransform == nullptr)
            continue;

        rig->Yaw -= ctx.Input.MouseDeltaX * rig->Sensitivity;
        rig->Pitch -= ctx.Input.MouseDeltaY * rig->Sensitivity;
        rig->Pitch = std::clamp(rig->Pitch, rig->MinPitch, rig->MaxPitch);

        Vec3d targetPosition = Vec3d::Zero();
        if (const WorldTransform* target = world.TryGet<WorldTransform>(rig->Target))
            targetPosition = target->Value.Position;

        const CameraPose pose = ComputeCameraPose(*rig, targetPosition);
        if (!pose.Override)
            continue;
        cameraTransform->Value.Position = pose.Position;
        cameraTransform->Value.Rotation = pose.Rotation;
    }
}
