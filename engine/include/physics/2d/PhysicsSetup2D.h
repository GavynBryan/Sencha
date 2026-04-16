#pragma once

class ServiceHost;
class SystemHost;

//=============================================================================
// PhysicsSetup2D
//
// Bootstrap helper for 2D physics systems. Call once during engine
// initialisation, after WorldSetup::Setup2D.
//
// Wires:
//   - ColliderSyncSystem2D as a system at phase 401
//     (just after TransformPropagationSystem at PostUpdate=400, so world
//      transforms are current before AABBs are computed)
//
// PhysicsDomain2D is owned by World2d::Physics — not a standalone service.
// Resolve it via serviceHost.Get<World2d>().Physics.
//
// PlayerMotorSystem2D (PreUpdate=200) and KinematicMoveSystem2D (phase 402)
// are game-specific. Add them from game code after calling Setup, using
// World2d::Physics and SystemHost::Get<ColliderSyncSystem2D>().
//=============================================================================
namespace PhysicsSetup2D {

    void Setup(ServiceHost& serviceHost, SystemHost& systemHost);

}
