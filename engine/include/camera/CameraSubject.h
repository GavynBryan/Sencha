#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>

#include <type_traits>

//=============================================================================
// CameraSubject
//
// On a camera entity: whose point of view it presents. A first-person eye and
// an orbit boom both present their pawn's; a fixed level camera presents
// nobody's. Presentation effects meant for one viewer -- a shake from that
// pawn's landing, a recoil -- reach only the view its subject owns. Set by the
// camera policy alongside the active camera.
//
// Runtime-only, like CameraExclusion: who a camera serves is a fact about the
// running process, so it carries no schema and no scene chunk.
//=============================================================================
struct SENCHA_COMPONENT("sencha.camera_subject") CameraSubject
{
    EntityId Subject;
};

static_assert(std::is_trivially_copyable_v<CameraSubject>,
              "CameraSubject must be trivially copyable to live in ECS chunks");

class World;

// Names whose view `camera` presents, adding the component on first use.
// Structural then, so outside any query.
void SetCameraSubject(World& world, EntityId camera, EntityId subject);

#if !defined(SENCHA_CODEGEN)
#  include <camera/CameraSubject.sencha.h>
#endif
