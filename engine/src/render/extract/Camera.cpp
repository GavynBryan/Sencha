#include <render/extract/Camera.h>

#include <camera/CameraExclusion.h>
#include <camera/PresentedFrame.h>
#include <time/FrameClock.h>
#include <world/transform/TransformComponents.h>

bool CameraRenderDataSystem::Build(const PresentedFrame& frame,
                                   const World& world,
                                   CameraRenderData& out)
{
    if (!frame.Valid)
    {
        return false;
    }

    // A shake moves the view, not the camera: its transform, and everything
    // that aims or traces from it, stays where the simulation put it. The
    // frustum below is built from the shaken view, so culling agrees with it.
    out.Entity = frame.Camera;
    out.View = frame.ViewMatrix;
    out.Projection = frame.ProjectionMatrix;
    out.ViewProjection = frame.ViewProjection;
    out.Position = frame.View.Pose.Position;
    out.ViewFrustum = Frustum::FromViewProjection(out.ViewProjection);
    // Exclusion is optional vocabulary: an editor viewport camera and a bare
    // authored camera have none, and their worlds never register the type. A
    // dead excluded entity excludes nothing rather than whatever recycled its
    // slot.
    out.ExcludedEntity = EntityId{};
    if (world.IsRegistered<CameraExclusion>())
    {
        if (const CameraExclusion* exclusion = world.TryGet<CameraExclusion>(frame.Camera))
        {
            if (exclusion->Excluded.IsValid() && world.IsAlive(exclusion->Excluded))
                out.ExcludedEntity = exclusion->Excluded;
        }
    }
    return true;
}

bool CameraRenderDataSystem::Build(const ActiveCameraService& activeCamera,
                                   const World& world,
                                   RenderExtent targetExtent,
                                   CameraRenderData& out,
                                   double presentationAlpha)
{
    if (!activeCamera.HasActive() || targetExtent.IsEmpty())
    {
        return false;
    }

    const EntityId entity = activeCamera.GetActive();
    if (!world.IsRegistered<WorldTransform>() || world.TryGet<WorldTransform>(entity) == nullptr)
    {
        return false;
    }

    PresentationTime presentation;
    presentation.Alpha = presentationAlpha;
    return Build(ResolvePresentedFrame(world, entity, targetExtent, presentation), world, out);
}
