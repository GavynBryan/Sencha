#pragma once

#include <physics/CollisionShapeCache.h>
#include <physics/PhysicsWorld.h>

struct PhysicsContext;
struct RegistryResidencyContext;

//=============================================================================
// PhysicsStepSystem
//
// Owns the one shared PhysicsWorld and orchestrates registry-local backend
// bindings. RegistryResidency runs once per rendered frame before the frame view:
// it evicts registries leaving physics, restores registries entering physics,
// and performs unconditional final teardown for Detaching registries. Physics
// then synchronizes every active registry, steps once, and pulls results back.
//=============================================================================
class PhysicsStepSystem
{
public:
    PhysicsStepSystem();
    ~PhysicsStepSystem();

    void RegistryResidency(RegistryResidencyContext& ctx);
    void Physics(PhysicsContext& ctx);

    [[nodiscard]] PhysicsWorld& GetSimulation() { return Simulation; }
    [[nodiscard]] CollisionShapeCache& GetShapeCache() { return Shapes; }

private:
    CollisionShapeCache Shapes;
    PhysicsWorld Simulation;
    int CollisionSteps = 1;
};
