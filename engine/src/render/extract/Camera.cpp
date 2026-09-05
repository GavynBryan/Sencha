#include <render/extract/Camera.h>

#include <camera/CameraExclusion.h>
#include <render/CameraProjection.h>
#include <world/transform/TransformComponents.h>

#include <cmath>

bool CameraRenderDataSystem::Build(const ActiveCameraService& activeCamera,
                                   const World& world,
                                   RenderExtent targetExtent,
                                   CameraRenderData& out)
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
    Mat4 projection;
    if (camera->Projection == ProjectionKind::Perspective)
    {
        projection = MakeVulkanPerspective(
            camera->FovYRadians, aspect, camera->NearPlane, camera->FarPlane);
    }
    else
    {
        const float halfHeight = camera->OrthographicHeight * 0.5f;
        const float halfWidth = halfHeight * aspect;
        projection = MakeVulkanOrthographic(
            -halfWidth, halfWidth, -halfHeight, halfHeight,
            camera->NearPlane, camera->FarPlane);
    }

    out.Entity = entity;
    out.View = transform->Value.ToMat4().AffineInverse();
    out.Projection = projection;
    out.ViewProjection = projection * out.View;
    out.Position = transform->Value.Position;
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
