#include <physics/2d/PhysicsSetup2D.h>

#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <physics/2d/ColliderSyncSystem2D.h>
#include <world/World.h>
#include <world/transform/TransformPropagationSystem.h>

namespace PhysicsSetup2D {

    void Setup(ServiceHost& serviceHost, SystemHost& systemHost)
    {
        World2d& world = serviceHost.Get<World2d>();

        // Fixed lane: ColliderSyncSystem2D must run after TransformPropagation
        // so world transforms are current before AABBs are computed.
        systemHost.Register<ColliderSyncSystem2D>(
            world.Transforms,
            world.Physics);

        systemHost.After<ColliderSyncSystem2D, TransformPropagationSystem<Transform2f>>();
    }

}
