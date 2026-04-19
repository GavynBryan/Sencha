#include <render/Camera.h>

#include <cmath>

namespace
{
    Mat4 MakeVulkanPerspective(float fovYRadians,
                               float aspect,
                               float nearPlane,
                               float farPlane)
    {
        const float tanHalfFov = std::tan(fovYRadians * 0.5f);
        Mat4 result;
        result[0][0] = 1.0f / (aspect * tanHalfFov);
        result[1][1] = -1.0f / tanHalfFov;  // Y-flip: Vulkan NDC has +Y pointing down
        result[2][2] = farPlane / (nearPlane - farPlane);  // reversed-Z: maps near->1, far->0
        result[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
        result[3][2] = -1.0f;
        return result;
    }

    Mat4 MakeVulkanOrthographic(float left,
                                float right,
                                float bottom,
                                float top,
                                float nearPlane,
                                float farPlane)
    {
        Mat4 result = Mat4::Identity();
        result[0][0] = 2.0f / (right - left);
        result[1][1] = -2.0f / (top - bottom);  // Y-flip
        result[2][2] = 1.0f / (nearPlane - farPlane);  // reversed-Z
        result[0][3] = -(right + left) / (right - left);
        result[1][3] = -(top + bottom) / (top - bottom);
        result[2][3] = nearPlane / (nearPlane - farPlane);
        return result;
    }
}

template <typename TTransformView>
bool BuildCameraRenderData(const ActiveCameraService& activeCamera,
                           const CameraStore& cameras,
                           const TTransformView& transforms,
                           VkExtent2D targetExtent,
                           CameraRenderData& out)
{
    if (!activeCamera.HasActive() || targetExtent.width == 0 || targetExtent.height == 0)
    {
        return false;
    }

    const EntityId entity = activeCamera.GetActive();
    const CameraComponent* camera = cameras.TryGet(entity);
    const Transform3f* transform = transforms.TryGetWorld(entity);
    if (camera == nullptr || transform == nullptr)
    {
        return false;
    }

    const float aspect = static_cast<float>(targetExtent.width)
                       / static_cast<float>(targetExtent.height);
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
    out.View = transform->ToMat4().AffineInverse();
    out.Projection = projection;
    out.ViewProjection = projection * out.View;
    out.Position = transform->Position;
    out.Frustum = Frustum::FromViewProjection(out.ViewProjection);
    return true;
}

bool CameraRenderDataSystem::Build(const ActiveCameraService& activeCamera,
                                   const CameraStore& cameras,
                                   const TransformStore<Transform3f>& transforms,
                                   VkExtent2D targetExtent,
                                   CameraRenderData& out)
{
    return BuildCameraRenderData(activeCamera, cameras, transforms, targetExtent, out);
}

bool CameraRenderDataSystem::Build(const ActiveCameraService& activeCamera,
                                   const CameraStore& cameras,
                                   const TransformPresentationStore<Transform3f>& transforms,
                                   VkExtent2D targetExtent,
                                   CameraRenderData& out)
{
    return BuildCameraRenderData(activeCamera, cameras, transforms, targetExtent, out);
}
