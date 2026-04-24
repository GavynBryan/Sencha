#pragma once

#include <array>
#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/service/IService.h>
#include <ecs/World.h>
#include <ecs/EntityId.h>
#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Frustum.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/SparseSetStore.h>
#include <vulkan/vulkan.h>

#include <string_view>
#include <tuple>

// Which projection formula a CameraComponent uses.
enum class ProjectionKind
{
    Perspective,
    Orthographic
};

template <>
struct EnumSchema<ProjectionKind>
{
    static constexpr std::array Values = {
        EnumValue{ ProjectionKind::Perspective, "perspective" },
        EnumValue{ ProjectionKind::Orthographic, "orthographic" },
    };
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

using CameraStore = SparseSetStore<CameraComponent>;

template <>
struct TypeSchema<CameraComponent>
{
    static constexpr std::string_view Name = "Camera";

    static auto Fields()
    {
        return std::tuple{
            MakeField("projection", &CameraComponent::Projection),
            MakeField("fov_y_radians", &CameraComponent::FovYRadians),
            MakeField("near_plane", &CameraComponent::NearPlane),
            MakeField("far_plane", &CameraComponent::FarPlane),
            MakeField("orthographic_height", &CameraComponent::OrthographicHeight)
                .Default(CameraComponent{}.OrthographicHeight),
        };
    }
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
