#pragma once

#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <ecs/World.h>
#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Frustum.h>
#include <math/geometry/3d/Transform3d.h>
#include <vulkan/vulkan.h>

//=============================================================================
// CameraRenderData
//
// Precomputed per-frame render matrices and frustum for the active camera.
// Built by CameraRenderDataSystem::Build() before extraction and passed
// read-only to render features and extraction systems.
//
// Matrices use Vulkan conventions: Y-flip and reversed-Z (near=1, far=0).
//=============================================================================
struct CameraRenderData
{
    EntityId Entity;
    Mat4 View = Mat4::Identity();
    Mat4 Projection = Mat4::Identity();
    Mat4 ViewProjection = Mat4::Identity();
    Vec3d Position;
    Frustum ViewFrustum;
};

//=============================================================================
// CameraRenderDataSystem
//
// Stateless system that builds a CameraRenderData from the active camera
// entity. Returns false if there is no active camera, the entity is missing
// required components, or the target extent is zero.
//=============================================================================
class CameraRenderDataSystem
{
public:
    [[nodiscard]] static bool Build(const ActiveCameraService& activeCamera,
                                    const World& world,
                                    VkExtent2D targetExtent,
                                    CameraRenderData& out);
};
