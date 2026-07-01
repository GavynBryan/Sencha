#pragma once

#include <core/metadata/EditorVisual.h>
#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>

#include <array>
#include <optional>
#include <string_view>
#include <tuple>

//=============================================================================
// CameraComponent  (neutral component catalog)
//
// Scene data describing a camera's projection. Lives in components/, not render/,
// so gameplay, the editor's scene authoring, and the cook can name it without
// pulling the renderer (Vulkan). The renderer consumes it (CameraRenderDataSystem
// in render/Camera.h); it does not own it.
//
// FovYRadians is ignored for Orthographic cameras; OrthographicHeight is ignored
// for Perspective cameras.
//=============================================================================
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
