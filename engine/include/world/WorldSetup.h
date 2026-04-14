#pragma once

class ServiceHost;
class SystemHost;

//=============================================================================
// WorldSetup
//
// Public bootstrap helpers for world-facing engine services.
//=============================================================================
namespace WorldSetup {

	void Setup2D(ServiceHost& serviceHost, SystemHost& systemHost);
	void Setup3D(ServiceHost& serviceHost, SystemHost& systemHost);
}
