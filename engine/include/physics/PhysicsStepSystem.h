#pragma once

#include <physics/CollisionShapeCache.h>
#include <physics/PhysicsWorld.h>

struct PhysicsContext;
struct RegistryResidencyContext;

//=============================================================================
// PhysicsStepSystem
//
// Frame orchestration for physics, scheduled in the Simulate phase's Physics
// step (it implements Physics(PhysicsContext&), detected by HasPhysics). Owns,
// by value, the one shared simulation and collision cache for all active zones.
// Per tick: sync each active registry's RigidBodyBinding into the world, step once at
// a fixed substep count, then sync resolved transforms back.
//
// The world and cache are plain members (no refcounting): they outlive every
// zone registry because EngineSchedule (which owns this system) is destroyed
// after ZoneRuntime (which owns the registries and their RigidBodyBindings). So a
// RigidBodyBinding can hold a raw PhysicsWorld* and clean up its bodies safely.
//=============================================================================
class PhysicsStepSystem
{
public:
    PhysicsStepSystem();
    ~PhysicsStepSystem();

    void Physics(PhysicsContext& ctx);

    // Lifecycle edges for retained physics state, dispatched by the
    // RegistryResidency frame phase: a registry leaving the physics domain
    // (dormancy or detach) evicts its backend objects while the registry is
    // still readable. Entering needs nothing here — eviction's link strips
    // bump the structural version, so the next sync's reconcile restores.
    void RegistryResidency(RegistryResidencyContext& ctx);

    [[nodiscard]] PhysicsWorld& GetSimulation() { return Simulation; }
    [[nodiscard]] CollisionShapeCache& GetShapeCache() { return Shapes; }

private:
    CollisionShapeCache Shapes;
    PhysicsWorld Simulation;
    int CollisionSteps = 1;
};
