#pragma once

#include <world/entity/EntityId.h>
#include <world/registry/RegistryId.h>

#include <cstdint>

enum class SelectableKind : uint8_t
{
    Entity = 0,
    BrushFace = 1,
};

struct SelectableRef
{
    RegistryId Registry = RegistryId::Invalid();
    EntityId Entity = {};
    SelectableKind Kind = SelectableKind::Entity;
    uint32_t ElementId = 0;

    [[nodiscard]] bool IsValid() const
    {
        return Registry.IsValid() && Entity.IsValid();
    }

    [[nodiscard]] bool IsEntity() const
    {
        return IsValid() && Kind == SelectableKind::Entity;
    }

    [[nodiscard]] bool IsBrushFace() const
    {
        return IsValid() && Kind == SelectableKind::BrushFace;
    }

    static SelectableRef EntitySelection(RegistryId registry, EntityId entity)
    {
        return SelectableRef{
            .Registry = registry,
            .Entity = entity,
            .Kind = SelectableKind::Entity,
            .ElementId = 0,
        };
    }

    static SelectableRef BrushFaceSelection(RegistryId registry, EntityId entity, uint32_t faceId)
    {
        return SelectableRef{
            .Registry = registry,
            .Entity = entity,
            .Kind = SelectableKind::BrushFace,
            .ElementId = faceId,
        };
    }

    bool operator==(const SelectableRef&) const = default;
};
