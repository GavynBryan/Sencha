#include <registry/Registry2DSetup.h>

#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <math/geometry/2d/Transform2d.h>
#include <registry/Registry2d.h>
#include <transform/TransformPropagationSystem.h>

namespace Registry2DSetup {

    namespace {
        Registry2d& GetOrAddRegistry2d(ServiceHost& serviceHost,
                                       const PhysicsConfig2D& physicsConfig)
        {
            if (auto* registry = serviceHost.TryGet<Registry2d>())
                return *registry;

            return serviceHost.AddService<Registry2d>(physicsConfig);
        }
    }

    void Setup2D(ServiceHost& serviceHost, SystemHost& systemHost,
                 const PhysicsConfig2D& physicsConfig)
    {
        Registry2d& registry = GetOrAddRegistry2d(serviceHost, physicsConfig);

        systemHost.Register<TransformPropagationSystem<Transform2f>>(
            registry.Transforms,
            registry.Hierarchy,
            registry.PropagationOrder);
    }
}
