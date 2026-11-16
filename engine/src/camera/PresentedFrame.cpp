#include <camera/PresentedFrame.h>

#include <camera/CameraEffects.h>
#include <components/ActiveCameraService.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <time/FrameClock.h>
#include <world/transform/PresentationOffset.h>
#include <world/transform/TransformHistory.h>

namespace
{
// Carries the view to what the camera holds. Added the first time a camera
// presents one, so a camera that is never offset carries none.
void WritePresentationOffset(World& world, EntityId camera, const Transform3f& offset)
{
    if (!camera.IsValid() || !world.IsAlive(camera) || !world.IsRegistered<PresentationOffset>())
        return;
    if (PresentationOffset* current = world.TryGet<PresentationOffset>(camera))
        current->Value = offset;
    else if (offset != Transform3f::Identity())
        world.AddComponent<PresentationOffset>(camera, PresentationOffset{ offset });
}
} // namespace

PresentedFrame ResolvePresentedFrame(const World& world,
                                     EntityId camera,
                                     RenderExtent extent,
                                     PresentationTime presentation)
{
    PresentedFrame frame;
    frame.FrameIndex = presentation.FrameIndex;
    frame.Alpha = presentation.Alpha;
    frame.SimulationSeconds = presentation.SimulationSeconds;
    frame.Extent = extent;
    frame.Camera = camera;
    if (extent.IsEmpty() || !camera.IsValid() || !world.IsAlive(camera) || !world.IsRegistered<CameraComponent>())
        return frame;
    const CameraComponent* component = world.TryGet<CameraComponent>(camera);
    if (component == nullptr)
        return frame;

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
    if (!extent.IsEmpty() && camera.IsValid() && world.IsAlive(camera))
        PresentCameraEffects(world, camera, clock, presentation.Alpha);

    PresentedFrame resolved = ResolvePresentedFrame(world, camera, extent, presentation);
    if (resolved.Valid && !visible.Contains(world.GetEntityPartition(camera)))
        resolved.Valid = false;
    if (published.Camera != camera)
        WritePresentationOffset(world, published.Camera, Transform3f::Identity());
    WritePresentationOffset(world, camera, resolved.Valid ? resolved.View.Offset : Transform3f::Identity());
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
