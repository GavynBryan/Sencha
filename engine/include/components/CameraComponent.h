#pragma once

#include <core/metadata/EnumSchema.h>
#include <ecs/ComponentAnnotations.h>

#include <array>

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

// Every field defaults to its member initializer, so a placed camera states
// only what it means to change -- a prefab's eye camera is a position and
// nothing else.
struct SENCHA_COMPONENT("Camera")
       SENCHA_SCHEMA("Camera")
       SENCHA_SCENE_CHUNK("CAMR")
       SENCHA_VISUAL_MESH("camera.glb")
CameraComponent
{
    SENCHA_FIELD("projection")
    ProjectionKind Projection = ProjectionKind::Perspective;

    SENCHA_FIELD("fov_y_radians")
    SENCHA_DEGREES
    SENCHA_LABEL("Field of view")
    SENCHA_TOOLTIP("Vertical angle the camera takes in. Ignored by an "
                   "orthographic camera.")
    float FovYRadians = 1.22173048f;

    SENCHA_FIELD("near_plane")
    float NearPlane = 0.1f;

    SENCHA_FIELD("far_plane")
    float FarPlane = 1000.0f;

    SENCHA_FIELD("orthographic_height")
    float OrthographicHeight = 10.0f;
};

#if !defined(SENCHA_CODEGEN)
#  include <components/CameraComponent.sencha.h>
#endif
