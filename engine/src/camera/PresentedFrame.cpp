#include <camera/PresentedFrame.h>

#include <camera/CameraEffects.h>
#include <components/ActiveCameraService.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <time/FrameClock.h>
#include <world/transform/TransformHistory.h>

PresentedFrame ResolvePresentedFrame(const World& world,
                                     EntityId camera,
                                     RenderExtent extent,
                                     PresentationTime presentation)
{
    PresentedFrame frame;
    frame.FrameIndex = presentation.FrameIndex;
    frame.Alpha = presentation.Alpha;
    frame.Extent = extent;
    frame.Camera = camera;
    if (extent.IsEmpty() || !camera.IsValid() || !world.IsAlive(camera) || !world.IsRegistered<CameraComponent>())
        return frame;
    const CameraComponent* component = world.TryGet<CameraComponent>(camera);
    if (component == nullptr)
        return frame;

    // Blended the way the camera is drawn: its own history when it carries one,
    // otherwise its local transform on its parent's drawn pose, which is the
    // pose presentation propagation is about to write for it.
    frame.Placed = PresentationPoseOf(world, camera, presentation.Alpha);
    frame.View = PresentCameraView(world, camera, *component, frame.Placed);
    frame.Projection = component->Projection;
    frame.NearPlane = component->NearPlane;
    frame.FarPlane = component->FarPlane;
    frame.OrthographicHeight = component->OrthographicHeight;

    const float aspect = static_cast<float>(extent.Width) / static_cast<float>(extent.Height);
    if (component->Projection == ProjectionKind::Perspective)
    {
        frame.ProjectionMatrix =
            MakeVulkanPerspective(frame.View.FovYRadians, aspect, component->NearPlane, component->FarPlane);
    }
    else
    {
        const float halfHeight = component->OrthographicHeight * 0.5f;
        const float halfWidth = halfHeight * aspect;
        frame.ProjectionMatrix = MakeVulkanOrthographic(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                                        component->NearPlane, component->FarPlane);
    }
    frame.ViewMatrix = frame.View.Pose.ToMat4().AffineInverse();
    frame.ViewProjection = frame.ProjectionMatrix * frame.ViewMatrix;
    frame.Valid = true;
    return frame;
}

const PresentedFrame& PublishPresentedFrame(World& world,
                                            const FixedSimulationLoop& clock,
                                            PresentationTime presentation,
                                            RenderExtent extent,
                                            const StoragePartitionSet& visible)
{
    if (!world.HasResource<PresentedFrame>())
        world.AddResource<PresentedFrame>();
    PresentedFrame& published = world.GetResource<PresentedFrame>();

    const ActiveCameraService* active = std::as_const(world).TryGetResource<ActiveCameraService>();
    const EntityId camera = active != nullptr && active->HasActive() ? active->GetActive() : EntityId{};
    // Nothing is presented where nothing is drawn: a headless host leaves the
    // effects as the simulation started them, as it always has.
    if (!extent.IsEmpty() && camera.IsValid() && world.IsAlive(camera))
        PresentCameraEffects(world, camera, clock, presentation.Alpha);

    PresentedFrame resolved = ResolvePresentedFrame(world, camera, extent, presentation);
    if (resolved.Valid && !visible.Contains(world.GetEntityPartition(camera)))
        resolved.Valid = false;
    resolved.Publication = published.Publication + 1;
    published = resolved;
    return published;
}

const PresentedFrame& EnsurePresentedFrame(World& world,
                                           const FixedSimulationLoop& clock,
                                           PresentationTime presentation,
                                           RenderExtent extent,
                                           const StoragePartitionSet& visible)
{
    if (const PresentedFrame* published = TryPresentedFrame(world))
    {
        const ActiveCameraService* active = std::as_const(world).TryGetResource<ActiveCameraService>();
        const EntityId camera = active != nullptr && active->HasActive() ? active->GetActive() : EntityId{};
        if (published->FrameIndex == presentation.FrameIndex && published->Extent == extent
            && published->Camera == camera && (!published->Valid || world.IsAlive(camera)))
        {
            return *published;
        }
    }
    return PublishPresentedFrame(world, clock, presentation, extent, visible);
}

const PresentedFrame* TryPresentedFrame(const World& world)
{
    return world.TryGetResource<PresentedFrame>();
}

ViewportProjection ProjectWorldPoint(const PresentedFrame& frame, const ViewportRect& viewport, const Vec3d& point)
{
    if (!frame.Valid)
        return {};
    return ProjectWorldPointToViewport(frame.ViewMatrix, frame.ProjectionMatrix, viewport, point);
}
