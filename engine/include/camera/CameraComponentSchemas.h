#pragma once

#include <camera/CameraSeat.h>
#include <components/CameraComponent.h>
#include <core/metadata/EditorVisual.h>
#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <math/MathSchemas.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape and editor presentation for the camera components.
//
// Registration and the serializers include this; the systems that read these
// components do not.
//=============================================================================

template <>
struct TypeSchema<CameraComponent>
{
    static constexpr std::string_view Name = "Camera";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('C', 'A', 'M', 'R');

    static auto Fields()
    {
        // Every field defaults to its member initializer, so a placed camera
        // states only what it means to change -- a prefab's eye camera is a
        // position and nothing else.
        const CameraComponent defaults;
        return std::tuple{
            MakeField("projection", &CameraComponent::Projection)
                .Default(defaults.Projection),
            MakeField("fov_y_radians", &CameraComponent::FovYRadians)
                .Default(defaults.FovYRadians)
                .Degrees()
                .Label("Field of view")
                .Tooltip("Vertical angle the camera takes in. Ignored by an "
                         "orthographic camera."),
            MakeField("near_plane", &CameraComponent::NearPlane)
                .Default(defaults.NearPlane),
            MakeField("far_plane", &CameraComponent::FarPlane)
                .Default(defaults.FarPlane),
            MakeField("orthographic_height", &CameraComponent::OrthographicHeight)
                .Default(defaults.OrthographicHeight),
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

template <>
struct TypeSchema<CameraSeat>
{
    static constexpr std::string_view Name = "CameraSeat";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('C', 'S', 'E', 'T');

    static auto Fields()
    {
        const CameraSeat defaults;
        return std::tuple{
            MakeField("role", &CameraSeat::Role).Default(defaults.Role),
            MakeField("mode", &CameraSeat::Mode)
                .Default(defaults.Mode)
                .Label("Watches from")
                .Tooltip("First person puts the view at the seat; third person "
                         "orbits the body at the distance below."),
            MakeField("distance", &CameraSeat::Distance)
                .Default(defaults.Distance)
                .Tooltip("Third person only: how far back the seat sits."),
        };
    }
};
