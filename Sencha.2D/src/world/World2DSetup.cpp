#include <world/World2DSetup.h>

#include <registry/Registry2DSetup.h>

namespace World2DSetup {

    void Setup2D(ServiceHost& serviceHost, SystemHost& systemHost,
                 const PhysicsConfig2D& physicsConfig)
    {
        Registry2DSetup::Setup2D(serviceHost, systemHost, physicsConfig);
    }
}
