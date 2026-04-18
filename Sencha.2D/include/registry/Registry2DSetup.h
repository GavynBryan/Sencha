#pragma once

#include <physics/PhysicsDomain2D.h>

class ServiceHost;
class SystemHost;

namespace Registry2DSetup {

    void Setup2D(ServiceHost& serviceHost, SystemHost& systemHost,
                 const PhysicsConfig2D& physicsConfig = {});
}
