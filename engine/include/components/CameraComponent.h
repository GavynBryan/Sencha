#pragma once

#include <core/metadata/EnumSchema.h>
#include <ecs/ComponentTypeId.h>

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
// in render/extract/Camera.h); it does not own it.
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

SENCHA_DECLARE_COMPONENT_TYPE(CameraComponent, "Camera");
SENCHA_COMPONENT_DECLARES_SCHEMA(CameraComponent);
