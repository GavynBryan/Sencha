#pragma once

#include <world/registry/EntityRef.h>
#include <world/registry/Registry.h>
#include <span>

// Domain-scoped lookup for simulation code. Callers must choose the span whose
// participation contract they actually need instead of resolving against the
// union of every active domain.
[[nodiscard]] inline Registry* FindRegistry(
    std::span<Registry* const> registries,
    RegistryId id)
{
    if (!id.IsValid())
        return nullptr;
    for (Registry* registry : registries)
        if (registry->Id == id)
            return registry;
    return nullptr;
}

[[nodiscard]] inline bool IsAliveIn(
    std::span<Registry* const> registries,
    EntityRef ref)
{
    const Registry* registry = FindRegistry(registries, ref.Registry);
    return registry != nullptr && registry->Components.IsAlive(ref.Entity);
}

//=============================================================================
// FrameRegistryView
//
// The frame's iteration view: which registries each domain visits this frame,
// built once at ScheduleTicks from drain-point-stable lifecycle state. Spans
// never contain null entries and stay valid until EndFrameView.
//=============================================================================
struct FrameRegistryView
{
    Registry* Global = nullptr;

    std::span<Registry*> Visible;
    std::span<Registry*> Physics;
    std::span<Registry*> Logic;
    std::span<Registry*> Audio;

    // Global plus every zone in at least one span this frame, in stable order.
    std::span<Registry* const> Participating;

    // Explicitly domain-agnostic resolution for systems that truly operate over
    // the union. Physics, logic, audio, and rendering code should instead call
    // FindRegistry with the matching domain span.
    [[nodiscard]] Registry* FindParticipating(RegistryId id) const
    {
        return FindRegistry(Participating, id);
    }

    [[nodiscard]] bool IsAliveParticipating(EntityRef ref) const
    {
        return IsAliveIn(Participating, ref);
    }
};
