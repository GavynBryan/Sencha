#pragma once

#include <physics/2d/PhysicsDomain2D.h>

class ServiceHost;
class SystemHost;

//=============================================================================
// World2DSetup
//
// Public bootstrap helpers for 2D world-facing engine services.
//=============================================================================
namespace World2DSetup {

	void Setup2D(ServiceHost& serviceHost, SystemHost& systemHost,
	             const PhysicsConfig2D& physicsConfig = {});
}
