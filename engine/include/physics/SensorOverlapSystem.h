#pragma once

class PhysicsStepSystem;
struct PhysicsContext;
struct ZoneResidencyContext;

class SensorOverlapSystem
{
public:
    explicit SensorOverlapSystem(PhysicsStepSystem& physics) : PhysicsScene(physics) {}
    void Physics(PhysicsContext& context);
    void ZoneResidency(ZoneResidencyContext& context);

private:
    PhysicsStepSystem& PhysicsScene;
};
