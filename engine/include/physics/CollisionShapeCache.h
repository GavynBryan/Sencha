#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include <physics/CollisionShape.h>

struct CollisionShapeCacheImpl;

//=============================================================================
// CollisionShapeCache
//
// Owns the loaded cooked collision shapes (Jolt mesh shapes) behind a PIMPL,
// keyed by an engine-typed CollisionShapeHandle. The collision-shape asset
// loader fills it; PhysicsWorld::AddBody resolves a Collider's handle through it
// when creating a static body. Backend-free in this header, so Collider and the
// loader registration stay Jolt-free.
//
// One cache shared across zones (collision shapes are content): held alongside
// the shared PhysicsWorld by PhysicsStepSystem.
//=============================================================================
class CollisionShapeCache
{
public:
    CollisionShapeCache();
    ~CollisionShapeCache();

    CollisionShapeCache(CollisionShapeCache&&) noexcept;
    CollisionShapeCache& operator=(CollisionShapeCache&&) noexcept;
    CollisionShapeCache(const CollisionShapeCache&) = delete;
    CollisionShapeCache& operator=(const CollisionShapeCache&) = delete;

    // Restore a shape from a pre-baked blob (the cooked path). Returns an invalid
    // handle if the blob is malformed.
    [[nodiscard]] CollisionShapeHandle LoadBlob(std::span<const std::byte> blob);

    [[nodiscard]] bool Has(CollisionShapeHandle handle) const;
    [[nodiscard]] std::size_t Count() const;

    // Backend access for PhysicsWorld (incomplete type here; usable only by
    // physics .cpp that include the internal header).
    [[nodiscard]] CollisionShapeCacheImpl& Internal() { return *Impl; }
    [[nodiscard]] const CollisionShapeCacheImpl& Internal() const { return *Impl; }

private:
    std::unique_ptr<CollisionShapeCacheImpl> Impl;
};
