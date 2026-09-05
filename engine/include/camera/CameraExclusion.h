#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>

#include <type_traits>

//=============================================================================
// CameraExclusion
//
// On a camera entity: the one entity this camera does not draw. Render
// extraction reads it and nothing else in the engine does; whoever owns the
// camera policy sets it -- a first-person view excludes the body it sits in, a
// fixed or orbiting one excludes nothing.
//
// Runtime-only. What a camera excludes follows from who is looking through it,
// which is a fact about the running process rather than about the authored
// scene, so it carries no schema and no scene chunk.
//=============================================================================
struct SENCHA_COMPONENT("sencha.camera_exclusion") CameraExclusion
{
    EntityId Excluded;
};

static_assert(std::is_trivially_copyable_v<CameraExclusion>,
              "CameraExclusion must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <camera/CameraExclusion.sencha.h>
#endif
