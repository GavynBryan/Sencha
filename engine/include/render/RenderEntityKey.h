#pragma once

#include <ecs/EntityId.h>
#include <world/registry/Registry.h>

#include <cstdint>

// Stable ordering identity for renderer-owned state. Zone-owned entities key on
// persistent ZoneId; other registry kinds use their runtime registry identity.
struct RenderEntityKey
{
    RegistryKind Kind = RegistryKind::Transient;
    ZoneId Zone;
    RegistryId RuntimeRegistry;
    EntityId Entity;

    [[nodiscard]] bool operator<(const RenderEntityKey& other) const
    {
        if (Kind != other.Kind)
            return static_cast<std::uint8_t>(Kind) < static_cast<std::uint8_t>(other.Kind);

        if (Kind == RegistryKind::Zone)
        {
            if (Zone != other.Zone)
                return Zone < other.Zone;
        }
        else
        {
            if (RuntimeRegistry.Index != other.RuntimeRegistry.Index)
                return RuntimeRegistry.Index < other.RuntimeRegistry.Index;
            if (RuntimeRegistry.Generation != other.RuntimeRegistry.Generation)
                return RuntimeRegistry.Generation < other.RuntimeRegistry.Generation;
        }

        if (Entity.Index != other.Entity.Index)
            return Entity.Index < other.Entity.Index;
        return Entity.Generation < other.Entity.Generation;
    }

    bool operator==(const RenderEntityKey&) const = default;
};

[[nodiscard]] inline RenderEntityKey MakeRenderEntityKey(
    const Registry& registry, EntityId entity)
{
    return RenderEntityKey{
        .Kind = registry.Kind,
        .Zone = registry.Kind == RegistryKind::Zone ? registry.Zone : ZoneId{},
        .RuntimeRegistry = registry.Kind == RegistryKind::Zone ? RegistryId{} : registry.Id,
        .Entity = entity,
    };
}
