#pragma once

#include <core/batch/DataBatch.h>
#include <core/batch/DataBatchKey.h>
#include <physics/Collider2D.h>
#include <physics/PhysicsDomain2D.h>
#include <physics/RigidBody2D.h>
#include <span>

//=============================================================================
// RigidBodyBatch2D
//
// Contiguous storage for RigidBody2D components. On Emplace, the body's
// collider is registered with PhysicsDomain2D and a Handle is returned.
// When the Handle destructs it unregisters from the domain and removes the
// body from the batch.
//
// Owned by World2d alongside PhysicsDomain2D. Lifetime must not exceed
// PhysicsDomain2D.
//
// Non-copyable and non-movable — Handle stores a raw pointer to this.
//
// Hot-path iteration: GetItems() returns a contiguous span of all live bodies.
// RigidBodySyncSystem2D and RigidBodyResolutionSystem2D iterate this span
// directly with no handle indirection in the inner loop.
//=============================================================================
class RigidBodyBatch2D
{
public:
    //=========================================================================
    // Handle
    //
    // RAII registration token for one RigidBody2D. Move-only. Destructor
    // removes the body from the batch and unregisters it from the domain.
    //=========================================================================
    class Handle
    {
    public:
        Handle() = default;
        ~Handle() { Release(); }

        Handle(Handle&& other) noexcept
            : Owner(other.Owner)
            , BatchKey(other.BatchKey)
            , BodyTransformKey(other.BodyTransformKey)
            , DomainHandle(other.DomainHandle)
        {
            other.Owner = nullptr;
        }

        Handle& operator=(Handle&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                Owner            = other.Owner;
                BatchKey         = other.BatchKey;
                BodyTransformKey = other.BodyTransformKey;
                DomainHandle     = other.DomainHandle;
                other.Owner      = nullptr;
            }
            return *this;
        }

        Handle(const Handle&)            = delete;
        Handle& operator=(const Handle&) = delete;

        bool IsValid() const { return Owner != nullptr; }

        // Transform key of the entity this body is linked to.
        // Required so entity structs holding a Handle can satisfy IsEntity.
        DataBatchKey TransformKey() const { return BodyTransformKey; }

    private:
        friend class RigidBodyBatch2D;

        Handle(RigidBodyBatch2D* owner,
               DataBatchKey      batchKey,
               DataBatchKey      transformKey,
               PhysicsHandle2D   domainHandle)
            : Owner(owner)
            , BatchKey(batchKey)
            , BodyTransformKey(transformKey)
            , DomainHandle(domainHandle)
        {}

        void Release();

        RigidBodyBatch2D* Owner          = nullptr;
        DataBatchKey      BatchKey;
        DataBatchKey      BodyTransformKey;
        PhysicsHandle2D   DomainHandle;
    };

    // -- Construction ---------------------------------------------------------

    explicit RigidBodyBatch2D(PhysicsDomain2D& physics);

    RigidBodyBatch2D(const RigidBodyBatch2D&)            = delete;
    RigidBodyBatch2D& operator=(const RigidBodyBatch2D&) = delete;
    RigidBodyBatch2D(RigidBodyBatch2D&&)                 = delete;
    RigidBodyBatch2D& operator=(RigidBodyBatch2D&&)      = delete;

    // -- Body management ------------------------------------------------------

    // Register a body linked to the given transform key. Returns a Handle that
    // must remain alive for the registration to stay active.
    Handle Emplace(DataBatchKey transformKey, const Collider2D& collider);

    // Random access — cold path. Use GetItems() for system iteration.
    RigidBody2D*       TryGet(const Handle& handle);
    const RigidBody2D* TryGet(const Handle& handle) const;

    // -- Contiguous iteration (for systems) -----------------------------------

    std::span<RigidBody2D>       GetItems();
    std::span<const RigidBody2D> GetItems() const;

    size_t Count()   const;
    bool   IsEmpty() const;

private:
    friend class Handle;
    void Remove(DataBatchKey batchKey, PhysicsHandle2D domainHandle);

    DataBatch<RigidBody2D> Batch;
    PhysicsDomain2D&       Physics;
};
