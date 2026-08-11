#include <camera/CameraFollowSystem.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <app/GameContexts.h>
#include <runtime/RuntimeFrameLoop.h>
#include <camera/CameraRig.h>
#include <components/ActiveCameraService.h>
#include <controller/LookOrientation.h>
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

    // The target aims; the camera presents what it is aiming at. A target with
    // no orientation of its own faces the world's default heading.
    float yaw = 0.0f;
    float pitch = 0.0f;
    if (const LookOrientation* look = world.TryGet<LookOrientation>(rig->Target))
    {
        yaw = look->Yaw;
        pitch = look->Pitch;

        // The aim integrates on the tick clock, so on a frame that ran no tick
        // it is the aim of the last one. Adding what the player's input owes
        // since then keeps the view tracking input at the rate frames arrive
        // rather than the rate ticks run. Displacement input (pointer) owes
        // the accumulated amount no tick has consumed; rate input (stick)
        // owes rate times the wall time the next tick has yet to absorb,
        // which is alpha of one fixed step -- derived, not accumulated, so it
        // shrinks to zero exactly as each tick lands and the view never steps
        // back.
        //
        // Only for a target this machine is aiming: anything else is aimed by
        // its own controller or by snapshots, and local input is not part of
        // where it is looking.
        const PendingLookInput* pending = world.TryGetResource<PendingLookInput>();
        if (pending != nullptr && world.HasComponent<LocalLookControl>(rig->Target))
        {
            const float rateTime = static_cast<float>(
                ctx.Presentation.Alpha * ctx.Presentation.DeltaSeconds);
            yaw += pending->Yaw + pending->RateYaw * rateTime;
            pitch = std::clamp(
                pitch + pending->Pitch + pending->RatePitch * rateTime,
                look->MinPitch, look->MaxPitch);
        }
    }

    const CameraPose pose = ComputeCameraPose(*rig, targetPosition, yaw, pitch);
    if (!pose.Override)
        return;

    // TEMPORARY DIAGNOSTIC (jitter investigation): per-frame aim trace.
    // SENCHA_AIM_TRACE=<path> appends one CSV row per frame. Delete when done.
    if (const char* tracePath = std::getenv("SENCHA_AIM_TRACE"))
    {
        static std::FILE* trace = []() -> std::FILE*
        {
            std::FILE* f = std::fopen(std::getenv("SENCHA_AIM_TRACE"), "w");
            if (f != nullptr)
                std::fputs("frame,rawDt,ticks,alpha,simYaw,pendingYaw,"
                           "presentedYaw,posX,posY,posZ\n", f);
            return f;
        }();
        (void)tracePath;
        if (trace != nullptr)
        {
            const RuntimeFrameSnapshot& frame = ctx.Runtime.GetCurrentFrame();
            float simYaw = 0.0f;
            float pendingYaw = 0.0f;
            if (const LookOrientation* look = world.TryGet<LookOrientation>(rig->Target))
                simYaw = look->Yaw;
            if (const PendingLookInput* pending = world.TryGetResource<PendingLookInput>())
                pendingYaw = pending->Yaw;
            std::fprintf(trace, "%llu,%.9f,%u,%.6f,%.9f,%.9f,%.9f,%.6f,%.6f,%.6f\n",
                         static_cast<unsigned long long>(frame.WallTime.FrameIndex),
                         static_cast<double>(frame.WallTime.UnscaledDt),
                         frame.FixedTicks,
                         ctx.Presentation.Alpha,
                         static_cast<double>(simYaw),
                         static_cast<double>(pendingYaw),
                         static_cast<double>(yaw),
                         targetPosition.X, targetPosition.Y, targetPosition.Z);
            std::fflush(trace);
        }
    }

    cameraTransform->Value.Position = pose.Position;
    cameraTransform->Value.Rotation = pose.Rotation;
}
