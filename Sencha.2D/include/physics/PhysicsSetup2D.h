#pragma once

class ServiceHost;
class SystemHost;

//=============================================================================
// PhysicsSetup2D
//
// Bootstrap helper for 2D physics systems. Call once during engine
// initialisation, after Registry2DSetup::Setup2D.
//
// Wires (all Fixed lane):
//   TransformPropagationSystem  (registered by Registry2DSetup::Setup2D)
//     -> RigidBodySyncSystem2D  (reads world transforms, updates domain bounds)
//       -> RigidBodyResolutionSystem2D (applies movement, writes local transforms)
//
// PhysicsDomain2D and RigidBodyStore are owned by Registry2d::Physics and
// Registry2d::Bodies respectively. Resolve via serviceHost.Get<Registry2d>().
//=============================================================================
namespace PhysicsSetup2D {

    void Setup(ServiceHost& serviceHost, SystemHost& systemHost);

}
