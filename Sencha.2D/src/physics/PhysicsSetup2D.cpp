#include <physics/PhysicsSetup2D.h>

#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <physics/RigidBodyResolutionSystem2D.h>
#include <physics/RigidBodySyncSystem2D.h>
#include <registry/Registry2d.h>
#include <transform/TransformPropagationSystem.h>

namespace PhysicsSetup2D {

    void Setup(ServiceHost& serviceHost, SystemHost& systemHost)
    {
        Registry2d& registry = serviceHost.Get<Registry2d>();

        systemHost.Register<RigidBodySyncSystem2D>(
            registry.Transforms,
            registry.Physics,
            registry.Bodies);

        systemHost.Register<RigidBodyResolutionSystem2D>(
            registry.Transforms,
            registry.Physics,
            registry.Bodies);

        systemHost.After<RigidBodySyncSystem2D, TransformPropagationSystem<Transform2f>>();
        systemHost.After<RigidBodyResolutionSystem2D, RigidBodySyncSystem2D>();
    }

}
