#pragma once

#include <core/batch/DataBatchKey.h>
#include <math/geometry/2d/Transform2d.h>
#include <physics/2d/Collider2D.h>
#include <physics/2d/PhysicsDomain2D.h>
#include <world/transform/TransformView.h>
#include <vector>

//=============================================================================
// ColliderSyncSystem2D
//
// Bridges the transform domain and the physics domain each frame. For every
// registered collider, it reads the entity's world Transform2D, computes the
// AABB from the Collider2D shape, pushes updated bounds into PhysicsDomain2D,
// and calls RebuildGrid once all bounds are current.
//
// Fixed-lane ordering (declared in PhysicsSetup2D::Setup via After<>):
//   TransformPropagationSystem (PostUpdate, low order)
//     -> ColliderSyncSystem2D  (PostUpdate, higher order)
//       -> KinematicMoveSystem2D (PostUpdate, highest order)
//
// Collider lifecycle:
//   PhysicsToken AddCollider(transformKey, Collider2D)
//     - allocates a slot in the internal batch
//     - registers with PhysicsDomain2D
//     - returns a PhysicsToken the owner must hold
//   RemoveCollider(PhysicsToken)
//     - deregisters from PhysicsDomain2D
//     - frees the slot
//
// PhysicsToken is a lightweight RAII wrapper. When it destructs it calls
// RemoveCollider automatically, so entities do not need explicit teardown.
//=============================================================================
class ColliderSyncSystem2D
{
public:
    //=========================================================================
    // PhysicsToken
    //
    // RAII registration token. Move-only. Destructor removes the collider.
    //=========================================================================
    class PhysicsToken
    {
    public:
        PhysicsToken() = default;
        ~PhysicsToken() { Release(); }

        PhysicsToken(PhysicsToken&& other) noexcept
            : Owner(other.Owner)
            , SlotIndex(other.SlotIndex)
            , PhysHandle(other.PhysHandle)
        {
            other.Owner = nullptr;
        }

        PhysicsToken& operator=(PhysicsToken&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                Owner      = other.Owner;
                SlotIndex  = other.SlotIndex;
                PhysHandle = other.PhysHandle;
                other.Owner = nullptr;
            }
            return *this;
        }

        PhysicsToken(const PhysicsToken&)            = delete;
        PhysicsToken& operator=(const PhysicsToken&) = delete;

        bool IsValid() const { return Owner != nullptr; }

    private:
        friend class ColliderSyncSystem2D;

        PhysicsToken(ColliderSyncSystem2D* owner,
                     uint32_t slotIndex,
                     PhysicsHandle2D physHandle)
            : Owner(owner)
            , SlotIndex(slotIndex)
            , PhysHandle(physHandle)
        {}

        void Release();

        ColliderSyncSystem2D* Owner      = nullptr;
        uint32_t              SlotIndex  = 0;
        PhysicsHandle2D       PhysHandle;
    };

    // -- Construction ----------------------------------------------------------

    ColliderSyncSystem2D(TransformView<Transform2f>& transforms,
                         PhysicsDomain2D& physicsDomain);

    // -- Collider management ---------------------------------------------------

    // Register a collider linked to a specific transform.
    // The returned PhysicsToken must be kept alive for the registration to
    // remain active — when the token is destroyed the collider is removed.
    PhysicsToken AddCollider(DataBatchKey transformKey, const Collider2D& collider);

    // Explicit removal. The token becomes invalid after this call.
    void RemoveCollider(PhysicsToken& token);

    // Read back the current collider shape for a live token (e.g. to adjust
    // half-extents at runtime). Returns nullptr if the token is invalid.
    Collider2D*       TryGetCollider(const PhysicsToken& token);
    const Collider2D* TryGetCollider(const PhysicsToken& token) const;

    void Tick(float fixedDt);

private:
    struct SyncSlot
    {
        DataBatchKey    TransformKey;
        Collider2D      Shape;
        PhysicsHandle2D PhysHandle;
        bool            Live = false;
    };

    TransformView<Transform2f>& Transforms;
    PhysicsDomain2D&            Physics;

    std::vector<SyncSlot>  Slots;
    std::vector<uint32_t>  FreeSlots;
};
