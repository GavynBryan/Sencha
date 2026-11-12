#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/logging/RepeatedFailureLog.h>

#include <physics/CharacterMoverPool.h>
#include <physics/CollisionShapeCache.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>

struct PhysicsContext;
struct ZoneResidencyContext;

// Owns the single retained physics scene for one simulation. Rigid bodies and
// character movers are simulation-wide backend records keyed by ordinary
// EntityId; storage partitions only control residency inside that scene.
class PhysicsStepSystem
{
public:
    PhysicsStepSystem();
    ~PhysicsStepSystem();

    void ZoneResidency(ZoneResidencyContext& ctx);
    void Physics(PhysicsContext& ctx);

    [[nodiscard]] PhysicsWorld& GetSimulation() { return Simulation; }
    [[nodiscard]] const PhysicsWorld& GetSimulation() const { return Simulation; }
    [[nodiscard]] CollisionShapeCache& GetShapeCache() { return Shapes; }
    [[nodiscard]] CharacterMoverPool& GetCharacterMovers()
    {
        return Characters;
    }
    [[nodiscard]] RigidBodyBinding& GetRigidBodies()
    {
        return Bodies;
    }
    void SetLog(Logger* log) { Log = log; }

private:
    CollisionShapeCache Shapes;
    PhysicsWorld Simulation;
    RigidBodyBinding Bodies;
    CharacterMoverPool Characters;
    int CollisionSteps = 1;
    Logger* Log = nullptr;
    PhysicsCapacityFailures ReportedFailures;
    RepeatedFailureLog CapacityLog;

    void ReportCapacityFailures();
};
