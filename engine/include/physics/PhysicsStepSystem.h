#pragma once

#include <physics/CollisionShapeCache.h>
#include <physics/PhysicsWorld.h>

struct PhysicsContext;

//=============================================================================
// PhysicsStepSystem
//
// Frame orchestration for physics, scheduled in the Simulate phase's Physics
// step (it implements Physics(PhysicsContext&), detected by HasPhysics). Owns,
// by value, the one shared simulation and collision cache for all active zones.
// Per tick: sync each active registry's PhysicsScene into the world, step once at
// a fixed substep count, then sync resolved transforms back.
//
// The world and cache are plain members (no refcounting): they outlive every
// zone registry because EngineSchedule (which owns this system) is destroyed
// after ZoneRuntime (which owns the registries and their PhysicsScenes). So a
// PhysicsScene can hold a raw PhysicsWorld* and clean up its bodies safely.
//=============================================================================
class PhysicsStepSystem
{
public:
    PhysicsStepSystem();
    ~PhysicsStepSystem();

    void Physics(PhysicsContext& ctx);

    [[nodiscard]] PhysicsWorld& GetSimulation() { return Simulation; }
    [[nodiscard]] CollisionShapeCache& GetShapeCache() { return Shapes; }

private:
    CollisionShapeCache Shapes;
    PhysicsWorld Simulation;
    int CollisionSteps = 1;
};
