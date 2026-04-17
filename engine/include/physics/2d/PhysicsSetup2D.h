#pragma once

class ServiceHost;
class SystemHost;

//=============================================================================
// PhysicsSetup2D
//
// Bootstrap helper for 2D physics systems. Call once during engine
// initialisation, after WorldSetup::Setup2D.
//
// Wires (all Fixed lane):
//   TransformPropagationSystem  (registered by WorldSetup::Setup2D)
//     -> RigidBodySyncSystem2D  (reads world transforms, updates domain bounds)
//       -> RigidBodyResolutionSystem2D (applies movement, writes local transforms)
//
// PhysicsDomain2D and RigidBodyBatch2D are owned by World2d::Physics and
// World2d::Bodies respectively. Resolve the world via serviceHost.Get<World2d>().
//=============================================================================
namespace PhysicsSetup2D {

    void Setup(ServiceHost& serviceHost, SystemHost& systemHost);

}
