#include <camera/CameraFollowSystem.h>

#include <algorithm>

#include <app/GameContexts.h>
#include <camera/CameraRig.h>
#include <components/ActiveCameraService.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

namespace
{
    // The active rig, or null when there is nothing to drive this frame. Both
    // phases resolve it the same way, so the guard lives in one place.
    CameraRig* ResolveActiveRig(World& world,
                                const StoragePartitionSet& partitions,
                                EntityId* outEntity = nullptr)
    {
        if (!world.IsRegistered<CameraRig>())
            return nullptr;

        const ActiveCameraService* cameraService =
            world.TryGetResource<ActiveCameraService>();
        if (cameraService == nullptr || !cameraService->HasActive())
            return nullptr;

        const EntityId cameraEntity = cameraService->GetActive();
        if (!world.IsAlive(cameraEntity)
            || !partitions.Contains(world.GetEntityPartition(cameraEntity)))
        {
            return nullptr;
        }

        if (outEntity != nullptr)
            *outEntity = cameraEntity;
        return world.TryGet<CameraRig>(cameraEntity);
    }
}

void CameraFollowSystem::PreSimulate(PreSimulateContext& ctx)
{
    CameraRig* rig = ResolveActiveRig(ctx.Entities, ctx.Partitions);
    if (rig == nullptr)
        return;

    // Before the tick, not after: a character steers along this orientation
    // during simulation, and accumulating it afterwards would aim every tick at
    // where the player was looking on the previous frame.
    rig->Yaw -= ctx.Input.MouseDeltaX * rig->Sensitivity;
    rig->Pitch -= ctx.Input.MouseDeltaY * rig->Sensitivity;
    rig->Pitch = std::clamp(rig->Pitch, rig->MinPitch, rig->MaxPitch);
}

void CameraFollowSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    World& world = ctx.Entities;
    EntityId cameraEntity;
    CameraRig* rig = ResolveActiveRig(world, ctx.Partitions, &cameraEntity);
    if (rig == nullptr)
        return;

    LocalTransform* cameraTransform = world.TryGet<LocalTransform>(cameraEntity);
    if (cameraTransform == nullptr)
        return;

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
