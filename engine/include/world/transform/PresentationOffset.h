#pragma once

#include <ecs/ComponentAnnotations.h>
#include <math/geometry/3d/Transform3d.h>

#include <type_traits>

//=============================================================================
// PresentationOffset
//
// A pose an entity's drawn pose carries on to the children that inherit it,
// composed between the two only when a frame is drawn. A camera's is the view
// its effects present, measured from the camera: a held model that inherits it
// stays where the picture is while the simulation still sees the camera.
// Runtime-only; the presented frame writes the active camera's.
//=============================================================================
struct SENCHA_COMPONENT("sencha.presentation_offset") PresentationOffset
{
    Transform3f Value = Transform3f::Identity();
};

static_assert(std::is_trivially_copyable_v<PresentationOffset>,
              "PresentationOffset must be trivially copyable to live in ECS chunks");

// On a child: drawn from its parent's pose through the parent's
// PresentationOffset. Presence is the whole value, so content opts a placed
// child in with no fields.
struct SENCHA_COMPONENT("sencha.inherits_presentation_offset")
       SENCHA_SCHEMA("InheritsPresentationOffset")
       SENCHA_SCENE_CHUNK("IPOF")
InheritsPresentationOffset
{
};

static_assert(std::is_empty_v<InheritsPresentationOffset>, "InheritsPresentationOffset is a tag: it carries no data");

#if !defined(SENCHA_CODEGEN)
#  include <world/transform/PresentationOffset.sencha.h>
#endif
