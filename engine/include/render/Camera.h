#pragma once

#include <array>
#include <core/metadata/EditorVisual.h>
#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/World.h>
#include <ecs/EntityId.h>
#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Frustum.h>
#include <math/geometry/3d/Transform3d.h>
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
// any entity that also has LocalTransform and WorldTransform.
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

template <>
struct TypeSchema<CameraComponent>
{
    static constexpr std::string_view Name = "Camera";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('C', 'A', 'M', 'R');

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

// The editor draws camera entities as a little camera mesh at their transform.
// Pure tooling metadata; the runtime ignores it. (core/metadata/EditorVisual.h)
template <>
struct ComponentEditorVisual<CameraComponent>
{
    static constexpr std::optional<EditorVisual> Value =
        EditorVisual{ EditorVisual::Kind::Mesh, "camera.glb" };
};

//=============================================================================
// ActiveCameraService
//
// Singleton service that tracks which entity is the active render camera.
// Only one camera can be active at a time. CameraRenderDataSystem reads this
// each frame to build CameraRenderData.
//=============================================================================
class ActiveCameraService
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
