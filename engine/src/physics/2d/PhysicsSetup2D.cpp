#include <physics/2d/PhysicsSetup2D.h>

#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <physics/2d/RigidBodyResolutionSystem2D.h>
#include <physics/2d/RigidBodySyncSystem2D.h>
#include <world/World.h>
#include <transform/TransformPropagationSystem.h>

namespace PhysicsSetup2D {

    void Setup(ServiceHost& serviceHost, SystemHost& systemHost)
    {
        World2d& world = serviceHost.Get<World2d>();

        systemHost.Register<RigidBodySyncSystem2D>(
            world.Transforms,
            world.Physics,
            world.Bodies);

        systemHost.Register<RigidBodyResolutionSystem2D>(
            world.Transforms,
            world.Physics,
            world.Bodies);

        systemHost.After<RigidBodySyncSystem2D, TransformPropagationSystem<Transform2f>>();
        systemHost.After<RigidBodyResolutionSystem2D, RigidBodySyncSystem2D>();
    }

}
