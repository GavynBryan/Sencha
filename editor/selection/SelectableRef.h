#pragma once

#include <ecs/EntityId.h>
#include <world/registry/RegistryId.h>

#include <cstdint>

enum class SelectableKind : uint8_t
{
    Entity = 0,
    Vertex = 1,
    Edge = 2,
    Face = 3,
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

    [[nodiscard]] bool IsVertex() const
    {
        return IsValid() && Kind == SelectableKind::Vertex;
    }

    [[nodiscard]] bool IsEdge() const
    {
        return IsValid() && Kind == SelectableKind::Edge;
    }

    [[nodiscard]] bool IsFace() const
    {
        return IsValid() && Kind == SelectableKind::Face;
    }

    [[nodiscard]] bool IsMeshElement() const
    {
        return IsVertex() || IsEdge() || IsFace();
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

    static SelectableRef VertexSelection(RegistryId registry, EntityId entity, uint32_t vertexId)
    {
        return SelectableRef{
            .Registry = registry,
            .Entity = entity,
            .Kind = SelectableKind::Vertex,
            .ElementId = vertexId,
        };
    }

    static SelectableRef EdgeSelection(RegistryId registry, EntityId entity, uint32_t edgeId)
    {
        return SelectableRef{
            .Registry = registry,
            .Entity = entity,
            .Kind = SelectableKind::Edge,
            .ElementId = edgeId,
        };
    }

    static SelectableRef FaceSelection(RegistryId registry, EntityId entity, uint32_t faceId)
    {
        return SelectableRef{
            .Registry = registry,
            .Entity = entity,
            .Kind = SelectableKind::Face,
            .ElementId = faceId,
        };
    }

    bool operator==(const SelectableRef&) const = default;
};
