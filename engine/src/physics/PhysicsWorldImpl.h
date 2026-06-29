#pragma once

// Internal Jolt-side header. Completes PhysicsWorld's PIMPL and holds the one
// Jolt instance plus the scaffolding it cannot exist without. Shared by every
// physics .cpp (world, scene, queries, character); never included by a public
// engine header, so no JPH type ever leaks to gameplay or ECS.

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <math/Quat.h>
#include <math/Vec.h>
#include <physics/CollisionShape.h>
#include <physics/PhysicsTypes.h>
#include "PhysicsLayers.h"

// 16 MB of scratch for one Update. Ample for room-scale zones; the allocator
// asserts rather than grows, which would surface here as a clear failure.
inline constexpr unsigned int kPhysicsTempAllocatorBytes = 16u * 1024u * 1024u;

struct PhysicsWorldImpl
{
    JPH::TempAllocatorImpl Temp;
    JPH::JobSystemSingleThreaded Jobs;
    BroadPhaseLayerInterfaceImpl BroadPhaseLayers;
    ObjectVsBroadPhaseLayerFilterImpl ObjectVsBroadPhase;
    ObjectLayerPairFilterImpl ObjectVsObject;
    JPH::PhysicsSystem System;

    PhysicsWorldImpl()
        : Temp(kPhysicsTempAllocatorBytes)
        , Jobs(JPH::cMaxPhysicsJobs)
    {
    }
};

// --- Backend-boundary conversions ------------------------------------------
// Engine math is float (Vec3d == Vec<3,float>), so these are plain field copies.

inline JPH::Vec3 ToJph(const Vec3d& v) { return JPH::Vec3(v.X, v.Y, v.Z); }
inline JPH::RVec3 ToJphR(const Vec3d& v) { return JPH::RVec3(v.X, v.Y, v.Z); }
inline JPH::Quat ToJph(const Quatf& q) { return JPH::Quat(q.X, q.Y, q.Z, q.W); }

inline Vec3d FromJph(JPH::Vec3Arg v) { return Vec3d(v.GetX(), v.GetY(), v.GetZ()); }
inline Vec3d FromJphR(JPH::RVec3Arg v) { return Vec3d(float(v.GetX()), float(v.GetY()), float(v.GetZ())); }
inline Quatf FromJph(JPH::QuatArg q) { return Quatf(q.GetX(), q.GetY(), q.GetZ(), q.GetW()); }

inline JPH::BodyID ToJph(PhysicsBodyId id) { return JPH::BodyID(id.Value); }
inline PhysicsBodyId FromJph(JPH::BodyID id) { return PhysicsBodyId{ id.GetIndexAndSequenceNumber() }; }

inline JPH::EMotionType ToJph(BodyMotion motion)
{
    switch (motion)
    {
    case BodyMotion::Static: return JPH::EMotionType::Static;
    case BodyMotion::Kinematic: return JPH::EMotionType::Kinematic;
    case BodyMotion::Dynamic: return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

// Build a backend shape from a primitive descriptor. Shared by body creation and
// the query view (sweeps/overlaps build a transient shape). Returns null on a
// degenerate descriptor; callers treat that as "no shape".
inline JPH::ShapeRefC BuildShape(const CollisionShape& shape)
{
    JPH::ShapeSettings::ShapeResult result;
    switch (shape.Type)
    {
    case CollisionShapeType::Sphere:
        result = JPH::SphereShapeSettings(shape.Radius).Create();
        break;
    case CollisionShapeType::Box:
        // Zero convex radius: keeps thin authored boxes (floors, walls) valid
        // instead of failing the half-extent >= convex-radius check.
        result = JPH::BoxShapeSettings(ToJph(shape.HalfExtents), 0.0f).Create();
        break;
    case CollisionShapeType::Capsule:
        result = JPH::CapsuleShapeSettings(shape.HalfHeight, shape.Radius).Create();
        break;
    }
    return result.IsValid() ? result.Get() : JPH::ShapeRefC();
}
