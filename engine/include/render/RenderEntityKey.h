#pragma once

#include <ecs/EntityId.h>

#include <cstdint>

// Stable ordering identity for renderer-owned state, keyed differently by its two
// producers.
//
// The runtime leaves Scope zero. One World means one entity namespace, so an
// EntityId already distinguishes entities across every streamed zone, and the
// comparison below reduces to EntityId order.
//
// Editor documents really are separate entity worlds, whose EntityIds collide
// with each other, so the editor supplies a per-document Scope. How one is
// derived is the editor's concern: see MakeRenderEntityKey in the editor's render
// layer. Scope is opaque here on purpose — the renderer only orders and compares
// it, and giving this header a document vocabulary would point it at the editor.
struct RenderEntityKey
{
    std::uint64_t Scope = 0;
    EntityId      Entity;

    [[nodiscard]] bool operator<(const RenderEntityKey& other) const
    {
        if (Scope != other.Scope)
            return Scope < other.Scope;
        if (Entity.Index != other.Entity.Index)
            return Entity.Index < other.Entity.Index;
        return Entity.Generation < other.Entity.Generation;
    }

    bool operator==(const RenderEntityKey&) const = default;
};
