#pragma once

#include <physics/2d/PhysicsDomain2D.h>

class ServiceHost;
class SystemHost;

//=============================================================================
// WorldSetup
//
// Public bootstrap helpers for world-facing engine services.
//=============================================================================
namespace WorldSetup {

	void Setup2D(ServiceHost& serviceHost, SystemHost& systemHost,
	             const PhysicsConfig2D& physicsConfig = {});
	void Setup3D(ServiceHost& serviceHost, SystemHost& systemHost);
}
