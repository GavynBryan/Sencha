#pragma once

#include <core/batch/SparseSet.h>
#include <core/service/IService.h>
#include <entity/EntityId.h>
#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Frustum.h>
#include <math/geometry/3d/Transform3d.h>
#include <transform/TransformStore.h>
#include <world/IComponentStore.h>
#include <vulkan/vulkan.h>

#include <span>
#include <vector>

// Which projection formula a CameraComponent uses.
enum class ProjectionKind
{
    Perspective,
    Orthographic
};

//=============================================================================
// CameraComponent
//
// ECS component that describes a camera's projection parameters. Attach to
// any entity that also has a Transform3f in a TransformStore.
//
// FovYRadians is ignored for Orthographic cameras; OrthographicHeight is
// ignored for Perspective cameras.
//=============================================================================
struct CameraComponent
{
    ProjectionKind Projection = ProjectionKind::Perspective;
    float FovYRadians = 1.22173048f;
    float NearPlane = 0.1f;
    float FarPlane = 1000.0f;
    float OrthographicHeight = 10.0f;
};

//=============================================================================
// CameraStore
//
// IComponentStore that maps EntityId -> CameraComponent. Backed by a
// SparseSet so iteration and lookup are O(1).
//=============================================================================
class CameraStore : public IComponentStore
{
public:
    bool Add(EntityId entity, const CameraComponent& component)
    {
        if (!entity.IsValid()) return false;
        Components.Emplace(entity.Index, component);
        return true;
    }

    bool Remove(EntityId entity)
    {
        return entity.IsValid() && Components.Remove(entity.Index);
    }

    [[nodiscard]] const CameraComponent* TryGet(EntityId entity) const
    {
        return entity.IsValid() ? Components.TryGet(entity.Index) : nullptr;
    }

private:
    SparseSet<CameraComponent> Components;
};

//=============================================================================
// ActiveCameraService
//
// Singleton service that tracks which entity is the active render camera.
// Only one camera can be active at a time. CameraRenderDataSystem reads this
// each frame to build CameraRenderData.
//=============================================================================
class ActiveCameraService : public IService
{
public:
    void SetActive(EntityId entity) { Active = entity; }
    [[nodiscard]] EntityId GetActive() const { return Active; }
    [[nodiscard]] bool HasActive() const { return Active.IsValid(); }

private:
    EntityId Active;
};

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
    Frustum Frustum;
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
                                    const CameraStore& cameras,
                                    const TransformStore<Transform3f>& transforms,
                                    VkExtent2D targetExtent,
                                    CameraRenderData& out);
};
