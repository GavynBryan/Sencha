#pragma once

#include <cstdint>
#include <cassert>
#include <ecs/World.h>
#include <world/registry/RegistryId.h>
#include <world/ResourceRegistry.h>
#include <zone/ZoneId.h>

enum class RegistryKind : uint8_t
{
    Global,
    Zone,
    Transient,
    Boundary
};

//=============================================================================
// RegistryEntityFacade
//
// Migration-only facade that preserves older `registry.Entities` call sites
// while entity ownership lives inside `registry.Components` (World).
//=============================================================================
struct RegistryEntityFacade
{
    explicit RegistryEntityFacade(World* world = nullptr)
        : Target(world)
    {
    }

    EntityId Create() { return Target->CreateEntity(); }
    void Destroy(EntityId entity) { Target->DestroyEntity(entity); }
    bool IsAlive(EntityId entity) const { return Target->IsAlive(entity); }
    size_t Count() const { return Target->EntityCount(); }
    std::vector<EntityId> GetAliveEntities() const { return Target->GetAliveEntities(); }

    World* Target = nullptr;
};

struct Registry
{
    Registry() = default;

    Registry(Registry&& other) noexcept
        : Id(other.Id)
        , Kind(other.Kind)
        , Zone(other.Zone)
        , Components(std::move(other.Components))
        , Resources(std::move(other.Resources))
        , Entities(&Components)
    {
    }

    Registry& operator=(Registry&& other) noexcept
    {
        Id = other.Id;
        Kind = other.Kind;
        Zone = other.Zone;
        Components = std::move(other.Components);
        Resources = std::move(other.Resources);
        Entities.Target = &Components;
        return *this;
    }

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    RegistryId Id;
    RegistryKind Kind = RegistryKind::Zone;
    ZoneId Zone;

    // Archetype ECS storage for this registry. The field name is kept during
    // the migration because much engine code already talks about a registry's
    // "components"; the type is now the ECS World, not a sparse-set registry.
    World Components;
    ResourceRegistry Resources;
    RegistryEntityFacade Entities{ &Components };
};

inline Registry MakeGlobalRegistry(RegistryId id = RegistryId::Global())
{
    assert(id.IsValid() && "MakeGlobalRegistry: registry id must be valid");
    assert(id.IsGlobal() && "MakeGlobalRegistry: global registry id must be RegistryId::Global");

    Registry registry;
    registry.Id = id;
    registry.Kind = RegistryKind::Global;
    registry.Zone = ZoneId::Invalid();

    assert(!registry.Zone.IsValid() && "Global registry must not have a ZoneId");
    return registry;
}

inline Registry MakeZoneRegistry(RegistryId id, ZoneId zone)
{
    assert(id.IsValid() && "MakeZoneRegistry: registry id must be valid");
    assert(zone.IsValid() && "Zone registry must have a valid ZoneId");

    Registry registry;
    registry.Id = id;
    registry.Kind = RegistryKind::Zone;
    registry.Zone = zone;

    assert(registry.Zone.IsValid() && "Zone registry must have a valid ZoneId");
    return registry;
}
