#pragma once

#include "PhysicsWorldImpl.h"

#include <ecs/EntityId.h>

class SensorOverlaps;

void ObserveSensorShape(const JPH::PhysicsSystem& system, const JPH::Shape& shape,
                        JPH::RMat44Arg end, const Vec3d& displacement,
                        EntityId activator, SensorOverlaps& observations);
