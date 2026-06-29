#pragma once

// Internal: Jolt's process-wide registration. Must run once before any Jolt
// allocation, shape build, or binary restore. Shared by PhysicsWorld and
// CollisionShapeCache (either can be the first to touch Jolt). Idempotent and
// intentionally never unregistered: the Factory is a process singleton, cheaper
// to leak at exit than to refcount.

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

inline void EnsureJoltRegistered()
{
    static const bool done = []
    {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        return true;
    }();
    (void)done;
}
