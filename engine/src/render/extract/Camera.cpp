#include <render/extract/Camera.h>

#include <camera/CameraExclusion.h>
#include <camera/CameraFovScale.h>
#include <camera/CameraShake.h>
#include <render/CameraProjection.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <algorithm>
#include <cmath>

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
    const CameraComponent* camera = world.TryGet<CameraComponent>(entity);
    const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
    if (camera == nullptr || transform == nullptr)
    {
        return false;
    }

    const float aspect = static_cast<float>(targetExtent.Width)
                       / static_cast<float>(targetExtent.Height);
    // Presented effects scale the camera's own field of view, never replace it.
    const CameraFovScaleState* fovScale = world.TryGetResource<CameraFovScaleState>();
    const float fovY = std::clamp(camera->FovYRadians * (fovScale != nullptr ? fovScale->ScaleFor(entity) : 1.0f),
                                  ToRadians(1.0f), ToRadians(179.0f));
    Mat4 projection;
    if (camera->Projection == ProjectionKind::Perspective)
    {
        projection = MakeVulkanPerspective(fovY, aspect, camera->NearPlane, camera->FarPlane);
    }
    else
    {
        const float halfHeight = camera->OrthographicHeight * 0.5f;
        const float halfWidth = halfHeight * aspect;
        projection = MakeVulkanOrthographic(
            -halfWidth, halfWidth, -halfHeight, halfHeight,
            camera->NearPlane, camera->FarPlane);
    }

    // A camera that carries pose history is drawn from the same blend every
    // mesh is, so it cannot step against the world it looks at. A parented
    // camera gets the blend through propagation instead and has no history.
    const Transform3f placed =
        world.IsRegistered<WorldTransformHistory>()
                && world.TryGet<WorldTransformHistory>(entity) != nullptr
            ? ResolvePresentationPose(*world.TryGet<WorldTransformHistory>(entity),
                                      presentationAlpha)
            : transform->Value;
    // A shake moves the view, not the camera: its transform, and everything
    // that aims or traces from it, stays where the simulation put it. The
    // frustum below is built from the shaken view, so culling agrees with it.
    const CameraShakeState* shake = world.TryGetResource<CameraShakeState>();
    const Transform3f pose = shake != nullptr ? ApplyCameraShake(placed, shake->OffsetFor(entity)) : placed;

    out.Entity = entity;
    out.View = pose.ToMat4().AffineInverse();
    out.Projection = projection;
    out.ViewProjection = projection * out.View;
    out.Position = pose.Position;
    out.ViewFrustum = Frustum::FromViewProjection(out.ViewProjection);
    // Exclusion is optional vocabulary: an editor viewport camera and a bare
    // authored camera have none, and their worlds never register the type. A
    // dead excluded entity excludes nothing rather than whatever recycled its
    // slot.
    out.ExcludedEntity = EntityId{};
    if (world.IsRegistered<CameraExclusion>())
    {
        if (const CameraExclusion* exclusion = world.TryGet<CameraExclusion>(entity))
        {
            if (exclusion->Excluded.IsValid() && world.IsAlive(exclusion->Excluded))
                out.ExcludedEntity = exclusion->Excluded;
        }
    }
    return true;
}
