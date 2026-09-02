#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>

//=============================================================================
// Per-mode session state
//
// A mode's session component is added on entry and removed on exit by the
// registry, so a mode that carries state is still just a registration.
//=============================================================================

struct SENCHA_COMPONENT("sencha.cling_session") ClingSession
{
    EntityId Surface;
    Vec3d SurfaceNormal = Vec3d(0.0f, -1.0f, 0.0f);
    Vec3d Anchor = Vec3d::Zero();
};

struct SENCHA_COMPONENT("sencha.flight_session") FlightSession
{
    uint8_t Active = 1;
};

#if !defined(SENCHA_CODEGEN)
#  include <movement/components/MovementSessions.sencha.h>
#endif
