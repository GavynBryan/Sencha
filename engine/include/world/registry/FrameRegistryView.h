#pragma once

#include <world/registry/EntityRef.h>
#include <world/registry/Registry.h>
#include <span>

//=============================================================================
// FrameRegistryView
//
// The frame's iteration view: which registries each domain visits this frame,
// built once at ScheduleTicks from drain-point-stable lifecycle state. Spans
// never contain null entries and stay valid until EndFrameView.
//
// Resolution is deliberately view-scoped: Find answers only for registries
// participating this frame, which is the correct default for simulation code —
// a dormant or detached registry is unresolvable, exactly as if it were gone.
// Residency-transition consumers get live pointers in their change records;
// tools and lifecycle orchestration use ZoneRuntime::FindRegistry.
//=============================================================================
struct FrameRegistryView
{
    Registry* Global = nullptr;

    std::span<Registry*> Visible;
    std::span<Registry*> Physics;
    std::span<Registry*> Logic;
    std::span<Registry*> Audio;

    // Global plus every zone in at least one span this frame, in span order
    // (global first, zones in attach order).
    std::span<Registry* const> Participating;

    // Resolve a registry participating this frame; null otherwise. Linear over
    // a handful of registries — index it only when a profile says so.
    [[nodiscard]] Registry* Find(RegistryId id) const
    {
        if (!id.IsValid())
            return nullptr;
        for (Registry* registry : Participating)
            if (registry->Id == id)
                return registry;
        return nullptr;
    }

    // Generational liveness through the view: true only when the ref's
    // registry participates this frame and the entity is alive in it.
    [[nodiscard]] bool IsAlive(EntityRef ref) const
    {
        const Registry* registry = Find(ref.Registry);
        return registry != nullptr && registry->Components.IsAlive(ref.Entity);
    }
};
