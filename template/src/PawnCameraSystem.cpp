#include "PawnCameraSystem.h"

#include <app/GameContexts.h>
#include <camera/CameraExclusion.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <controller/LookOrientation.h>
#include <core/logging/Logger.h>
#include <ecs/World.h>
#include <participant/LocalControl.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>

namespace
{
// The camera a body carries: a child with a CameraComponent. One per pawn is
// this game's authoring rule; a prefab with several would want a marker, and
// gets the first until it has one.
EntityId CameraChildOf(const World& world, EntityId body)
{
    if (!world.IsRegistered<CameraComponent>() || !world.IsRegistered<Parent>())
        return EntityId{};

    for (const EntityId entity : world.GetAliveEntities())
    {
        const Parent* parent = world.TryGet<Parent>(entity);
        if (parent != nullptr && parent->Entity == body
            && world.HasComponent<CameraComponent>(entity))
        {
            return entity;
        }
    }
    return EntityId{};
}
} // namespace

void PawnCameraSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    World& world = ctx.Entities;
    const EntityId subject = LocalControlSubjectOf(world);

    if (subject != Body)
    {
        Body = subject;
        Camera = subject.IsValid() ? CameraChildOf(world, subject) : EntityId{};

        ActiveCameraService& active = world.GetResource<ActiveCameraService>();
        active.SetActive(Camera);
        if (Camera.IsValid())
        {
            // Looking out from inside the body: the body is not in the picture.
            if (world.IsRegistered<CameraExclusion>())
            {
                if (CameraExclusion* exclusion = world.TryGet<CameraExclusion>(Camera))
                    exclusion->Excluded = Body;
                else
                    world.AddComponent<CameraExclusion>(Camera, CameraExclusion{ Body });
            }
            if (Log != nullptr)
                Log->Info("TemplateGame: local player attached to its pawn");
        }
        else if (subject.IsValid() && Log != nullptr)
        {
            Log->Warn("TemplateGame: the driven body carries no camera child; "
                      "nothing is looking through it");
        }
    }

    if (!Camera.IsValid() || !world.IsAlive(Camera) || !world.IsAlive(Body))
        return;

    // Yaw and position come through the hierarchy from the body. Pitch is the
    // camera's own, taken from the body's aim plus whatever this machine's
    // look input has accumulated since the last tick, so the view tracks the
    // pointer at the rate frames arrive rather than the rate ticks run.
    const LookOrientation* look = world.TryGet<LookOrientation>(Body);
    LocalTransform* transform = world.TryGet<LocalTransform>(Camera);
    if (look == nullptr || transform == nullptr)
        return;

    float pitch = look->Pitch;
    const PendingLookInput* pending = world.TryGetResource<PendingLookInput>();
    if (pending != nullptr && world.IsRegistered<LocalLookControl>()
        && world.HasComponent<LocalLookControl>(Body))
    {
        const float rateTime = static_cast<float>(
            ctx.Presentation.Alpha * ctx.Presentation.DeltaSeconds);
        pitch = std::clamp(pitch + pending->Pitch + pending->RatePitch * rateTime,
                           look->MinPitch, look->MaxPitch);
    }
    transform->Value.Rotation = Quatf::FromAxisAngle(Vec3d::Right(), pitch);
}
