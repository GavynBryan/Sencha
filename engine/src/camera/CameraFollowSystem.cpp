#include <camera/CameraFollowSystem.h>

#include <algorithm>

#include <app/GameContexts.h>
#include <camera/CameraRig.h>
#include <components/ActiveCameraService.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

void CameraFollowSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    World& world = ctx.Entities;
    if (!world.IsRegistered<CameraRig>())
        return;

    const ActiveCameraService* cameraService =
        world.TryGetResource<ActiveCameraService>();
    if (cameraService == nullptr || !cameraService->HasActive())
        return;

    const EntityId cameraEntity = cameraService->GetActive();
    if (!world.IsAlive(cameraEntity)
        || !ctx.Partitions.Contains(world.GetEntityPartition(cameraEntity)))
    {
        return;
    }

    CameraRig* rig = world.TryGet<CameraRig>(cameraEntity);
    LocalTransform* cameraTransform =
        world.TryGet<LocalTransform>(cameraEntity);
    if (rig == nullptr || cameraTransform == nullptr)
        return;

    rig->Yaw -= ctx.Input.MouseDeltaX * rig->Sensitivity;
    rig->Pitch -= ctx.Input.MouseDeltaY * rig->Sensitivity;
    rig->Pitch = std::clamp(rig->Pitch, rig->MinPitch, rig->MaxPitch);

    // Follow the pose the target is being drawn at, not the one the last tick
    // left it in: chasing the tick pose would reintroduce the step this frame's
    // interpolation exists to remove.
    Vec3d targetPosition = Vec3d::Zero();
    if (const WorldTransformHistory* history =
            world.TryGet<WorldTransformHistory>(rig->Target))
    {
        targetPosition =
            ResolvePresentationPose(*history, ctx.Presentation.Alpha).Position;
    }
    else if (const WorldTransform* target = world.TryGet<WorldTransform>(rig->Target))
    {
        targetPosition = target->Value.Position;
    }

    const CameraPose pose = ComputeCameraPose(*rig, targetPosition);
    if (!pose.Override)
        return;

    cameraTransform->Value.Position = pose.Position;
    cameraTransform->Value.Rotation = pose.Rotation;
}
