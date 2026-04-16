#include <physics/2d/PhysicsSetup2D.h>

#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <core/system/SystemPhase.h>
#include <physics/2d/ColliderSyncSystem2D.h>
#include <world/World.h>

namespace PhysicsSetup2D {

    void Setup(ServiceHost& serviceHost, SystemHost& systemHost)
    {
        // Physics is owned by the world — no separate service registration.
        World2d& world = serviceHost.Get<World2d>();

        // ColliderSyncSystem2D runs at phase 401 — after TransformPropagation
        // at PostUpdate(400), before any kinematic move systems at 402.
        systemHost.AddSystem<ColliderSyncSystem2D>(
            static_cast<SystemPhase>(401),
            world.Transforms,
            world.Physics);
    }

}
