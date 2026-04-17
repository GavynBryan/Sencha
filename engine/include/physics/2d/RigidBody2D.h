#pragma once

#include <core/batch/DataBatchKey.h>
#include <math/Vec.h>
#include <physics/2d/Collider2D.h>
#include <physics/2d/PhysicsDomain2D.h>

//=============================================================================
// RigidBody2D
//
// Kinematic physics component stored in DataBatch<RigidBody2D>. Entities hold
// a DataBatchHandle<RigidBody2D>; when it destructs the item is removed from
// the batch, the destructor fires, and the domain registration is cleaned up.
//
// Constructing with the three-argument form registers with PhysicsDomain2D
// immediately. The domain pointer is stored solely for cleanup on destruction —
// it is not accessed during normal simulation.
//
// Move-only. The domain pointer is cleared in the move source so that only
// the live owner ever calls Unregister, even when DataBatch rearranges slots
// via swap-and-pop or bulk removal.
//
// Velocity is in world-space units per second. Gameplay writes it each frame;
// RigidBodyResolutionSystem2D consumes it, applies move-and-slide, writes the
// resolved delta back into the local transform, then zeroes Velocity.
//=============================================================================
struct RigidBody2D
{
    DataBatchKey    TransformKey;
    Collider2D      Shape;
    Vec2d           Velocity  = { 0.0f, 0.0f };
    HitFlags2D      LastHits  = HitFlags2D::None;
    PhysicsHandle2D DomainHandle;

    RigidBody2D() = default;

    RigidBody2D(PhysicsDomain2D& domain, DataBatchKey transformKey, const Collider2D& collider)
        : TransformKey(transformKey)
        , Shape(collider)
        , DomainHandle(domain.Register(collider))
        , Domain(&domain)
    {}

    ~RigidBody2D()
    {
        if (Domain && DomainHandle.IsValid())
            Domain->Unregister(DomainHandle);
    }

    RigidBody2D(RigidBody2D&& o) noexcept
        : TransformKey(o.TransformKey)
        , Shape(o.Shape)
        , Velocity(o.Velocity)
        , LastHits(o.LastHits)
        , DomainHandle(o.DomainHandle)
        , Domain(o.Domain)
    {
        o.Domain = nullptr;
    }

    RigidBody2D& operator=(RigidBody2D&& o) noexcept
    {
        if (this != &o)
        {
            if (Domain && DomainHandle.IsValid())
                Domain->Unregister(DomainHandle);
            TransformKey = o.TransformKey;
            Shape        = o.Shape;
            Velocity     = o.Velocity;
            LastHits     = o.LastHits;
            DomainHandle = o.DomainHandle;
            Domain       = o.Domain;
            o.Domain     = nullptr;
        }
        return *this;
    }

    RigidBody2D(const RigidBody2D&)            = delete;
    RigidBody2D& operator=(const RigidBody2D&) = delete;

private:
    PhysicsDomain2D* Domain = nullptr;
};
