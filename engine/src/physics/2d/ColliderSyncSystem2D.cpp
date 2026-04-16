#include <physics/2d/ColliderSyncSystem2D.h>

#include <math/geometry/2d/Aabb2d.h>

// ---------------------------------------------------------------------------
// PhysicsToken::Release
// ---------------------------------------------------------------------------

void ColliderSyncSystem2D::PhysicsToken::Release()
{
    if (Owner)
    {
        Owner->Physics.Unregister(PhysHandle);
        if (SlotIndex < Owner->Slots.size())
        {
            Owner->Slots[SlotIndex].Live = false;
            Owner->FreeSlots.push_back(SlotIndex);
        }
        Owner = nullptr;
    }
}

// ---------------------------------------------------------------------------
// ColliderSyncSystem2D
// ---------------------------------------------------------------------------

ColliderSyncSystem2D::ColliderSyncSystem2D(TransformView<Transform2f>& transforms,
                                           PhysicsDomain2D& physicsDomain)
    : Transforms(transforms)
    , Physics(physicsDomain)
{}

ColliderSyncSystem2D::PhysicsToken
ColliderSyncSystem2D::AddCollider(DataBatchKey transformKey,
                                  const Collider2D& collider)
{
    PhysicsHandle2D physHandle = Physics.Register(collider);

    uint32_t slotIndex;
    if (!FreeSlots.empty())
    {
        slotIndex = FreeSlots.back();
        FreeSlots.pop_back();
        Slots[slotIndex] = { transformKey, collider, physHandle, true };
    }
    else
    {
        slotIndex = static_cast<uint32_t>(Slots.size());
        Slots.push_back({ transformKey, collider, physHandle, true });
    }

    return PhysicsToken(this, slotIndex, physHandle);
}

void ColliderSyncSystem2D::RemoveCollider(PhysicsToken& token)
{
    token.Release();
}

Collider2D* ColliderSyncSystem2D::TryGetCollider(const PhysicsToken& token)
{
    if (!token.IsValid() || token.SlotIndex >= Slots.size()) return nullptr;
    SyncSlot& slot = Slots[token.SlotIndex];
    return slot.Live ? &slot.Shape : nullptr;
}

const Collider2D* ColliderSyncSystem2D::TryGetCollider(const PhysicsToken& token) const
{
    if (!token.IsValid() || token.SlotIndex >= Slots.size()) return nullptr;
    const SyncSlot& slot = Slots[token.SlotIndex];
    return slot.Live ? &slot.Shape : nullptr;
}

// ---------------------------------------------------------------------------
// Update: sync world transforms -> AABB bounds -> PhysicsDomain2D
// ---------------------------------------------------------------------------

void ColliderSyncSystem2D::Tick(float /*fixedDt*/)
{
    for (SyncSlot& slot : Slots)
    {
        if (!slot.Live) continue;

        const Transform2f* world = Transforms.TryGetWorld(slot.TransformKey);
        if (!world) continue;

        // Compute world-space AABB: rotate the half-extent box by the
        // transform's rotation to get an axis-aligned envelope.
        //
        // For v0 (AABB-only, no rotation-based collider handling) we treat
        // scale but ignore rotation — the collider stays axis-aligned in
        // world space. This is explicitly documented as a v0 limitation.
        const Vec2d scaledHalf = {
            slot.Shape.HalfExtent.X * world->Scale.X,
            slot.Shape.HalfExtent.Y * world->Scale.Y
        };

        const Vec2d center = {
            world->Position.X + slot.Shape.Offset.X,
            world->Position.Y + slot.Shape.Offset.Y
        };

        Aabb2d worldBounds = Aabb2d::FromCenterHalfExtent(center, scaledHalf);

        slot.Shape.WorldBounds = worldBounds;
        Physics.UpdateBounds(slot.PhysHandle, worldBounds);
    }

    Physics.RebuildTree();
}
