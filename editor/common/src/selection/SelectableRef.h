#pragma once

#include <core/identity/Id.h>
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

//=============================================================================
// SelectableRef
//
// What is selected: an entity, or one mesh element on one entity.
//
// Two ways to say which entity, and they are not equals. `Stable` is the
// document's own identity for it and survives anything that rebuilds entity
// storage -- a scene-instance placement, a source reload, an undo. `Entity`
// is the live generational handle, which does not: recreating an entity
// leaves every handle to it dead. So the handle is a CACHE that the selection
// service stamps and refreshes, and the persistent id is the identity that
// equality is keyed on whenever both refs carry one.
//
// A ref built outside the selection (a pick result, a query) starts with no
// stable id and is stamped on the way in; comparing such a ref against a
// stored one falls back to the handle, which is correct because the stored
// handle is kept fresh.
//=============================================================================
struct SelectableRef
{
    RegistryId Registry = RegistryId::Invalid();
    EntityId Entity = {};
    // The document identity of `Entity`, stamped when the ref enters the
    // selection. Invalid on a freshly built ref and on entities a document
    // does not identify.
    PersistentEntityId Stable = {};
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

    // Identity comparison, not member comparison: two refs naming the same
    // element of the same entity are equal even when one was captured before
    // a rebuild and the other after, because the handle is only a cache.
    friend bool operator==(const SelectableRef& left, const SelectableRef& right)
    {
        if (left.Registry != right.Registry || left.Kind != right.Kind
            || left.ElementId != right.ElementId)
        {
            return false;
        }
        if (left.Stable.IsValid() && right.Stable.IsValid())
            return left.Stable == right.Stable;
        return left.Entity == right.Entity;
    }
};
