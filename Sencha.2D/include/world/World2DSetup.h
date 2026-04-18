#pragma once

#include <physics/PhysicsDomain2D.h>

class ServiceHost;
class SystemHost;

//=============================================================================
// World2DSetup
//
// Deprecated compatibility wrapper for Registry2DSetup.
//=============================================================================
namespace World2DSetup {

	void Setup2D(ServiceHost& serviceHost, SystemHost& systemHost,
	             const PhysicsConfig2D& physicsConfig = {});
}
