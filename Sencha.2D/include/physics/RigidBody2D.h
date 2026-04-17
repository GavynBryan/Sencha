#pragma once

#include <core/batch/SparseSet.h>
#include <entity/EntityHandle.h>
#include <math/Vec.h>
#include <physics/Collider2D.h>
#include <physics/PhysicsDomain2D.h>
#include <world/IComponentStore.h>
#include <cstddef>
#include <span>
#include <vector>

//=============================================================================
// RigidBody2D
//
// Kinematic physics component stored in RigidBodyStore. The owning entity lives
// in SparseSet's parallel owner list, so the component only carries physics
// state.
//
// Velocity is in world-space units per second. Gameplay writes it each frame;
// RigidBodyResolutionSystem2D consumes it, applies move-and-slide, writes the
// resolved delta back into the local transform, then zeroes Velocity.
//=============================================================================
struct RigidBody2D
{
    Collider2D      Shape;
    Vec2d           Velocity  = { 0.0f, 0.0f };
    HitFlags2D      LastHits  = HitFlags2D::None;

    RigidBody2D() = default;

    explicit RigidBody2D(const Collider2D& collider)
        : Shape(collider)
    {}
};

//=============================================================================
// RigidBodyStore
//
// Entity-indexed rigid-body component store. Static bodies are mirrored into
// PhysicsDomain2D as broadphase blockers; dynamic bodies remain component data
// and query the domain during resolution.
//=============================================================================
class RigidBodyStore : public IComponentStore
{
public:
    explicit RigidBodyStore(PhysicsDomain2D& physics)
        : Physics(&physics)
    {
    }

    ~RigidBodyStore() override
    {
        Clear();
    }

    RigidBodyStore(const RigidBodyStore&) = delete;
    RigidBodyStore& operator=(const RigidBodyStore&) = delete;
    RigidBodyStore(RigidBodyStore&&) = delete;
    RigidBodyStore& operator=(RigidBodyStore&&) = delete;

    bool Add(EntityHandle entity, const Collider2D& collider = {})
    {
        if (!entity.IsValid())
            return false;

        if (RigidBody2D* existing = Components.TryGet(entity.Id))
            Unregister(entity);

        RigidBody2D& body = Components.Emplace(entity.Id, collider);
        EnsureDomainRegistration(entity, body);
        return true;
    }

    bool Remove(EntityHandle entity)
    {
        if (!entity.IsValid())
            return false;

        RigidBody2D* body = Components.TryGet(entity.Id);
        if (!body)
            return false;

        Unregister(entity);
        return Components.Remove(entity.Id);
    }

    void Clear()
    {
        const std::vector<Id>& owners = Components.GetOwners();
        std::vector<RigidBody2D>& bodies = Components.GetItems();
        for (size_t i = 0; i < bodies.size(); ++i)
            Unregister(EntityHandle{ owners[i], 0 });
        Components.Clear();
    }

    bool Contains(EntityHandle entity) const
    {
        return entity.IsValid() && Components.Contains(entity.Id);
    }

    RigidBody2D* TryGet(EntityHandle entity)
    {
        return entity.IsValid() ? Components.TryGet(entity.Id) : nullptr;
    }

    const RigidBody2D* TryGet(EntityHandle entity) const
    {
        return entity.IsValid() ? Components.TryGet(entity.Id) : nullptr;
    }

    void EnsureDomainRegistration(EntityHandle entity, RigidBody2D& body)
    {
        if (!Physics)
            return;

        if (body.Shape.IsStatic)
        {
            if (!Physics->Contains(entity))
                Physics->Register(entity, body.Shape);
        }
        else
        {
            Physics->Unregister(entity);
        }
    }

    std::span<RigidBody2D> GetItems()
    {
        auto& items = Components.GetItems();
        return std::span<RigidBody2D>(items.data(), items.size());
    }

    std::span<const RigidBody2D> GetItems() const
    {
        const auto& items = Components.GetItems();
        return std::span<const RigidBody2D>(items.data(), items.size());
    }

    const std::vector<Id>& GetOwners() const { return Components.GetOwners(); }

    size_t Count() const { return Components.Count(); }
    bool IsEmpty() const { return Components.IsEmpty(); }
    uint64_t GetVersion() const { return Components.GetVersion(); }

private:
    void Unregister(EntityHandle entity)
    {
        if (Physics)
            Physics->Unregister(entity);
    }

    PhysicsDomain2D* Physics = nullptr;
    SparseSet<RigidBody2D> Components;
};
