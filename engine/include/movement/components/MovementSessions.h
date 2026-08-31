#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>

//=============================================================================
// Per-mode session state
//
// A mode's session component is added on entry and removed on exit by the
// registry, so a mode that carries state is still just a registration.
//=============================================================================

struct ClingSession
{
    EntityId Surface;
    Vec3d SurfaceNormal = Vec3d(0.0f, -1.0f, 0.0f);
    Vec3d Anchor = Vec3d::Zero();
};
SENCHA_DECLARE_COMPONENT_TYPE(ClingSession, "sencha.cling_session");

struct FlightSession
{
    uint8_t Active = 1;
};
SENCHA_DECLARE_COMPONENT_TYPE(FlightSession, "sencha.flight_session");
