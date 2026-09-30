#pragma once

#include <render/RenderEntityKey.h>
#include <world/registry/Registry.h>

#include <cassert>
#include <cstdint>

// Per-document scope for renderer-owned state. The editor renders several
// documents through one renderer and their EntityIds collide, so every key
// carries the document it came from.
//
// A zone document keys on its persistent ZoneId, so renderer state survives
// reloading that document; any other document keys on its runtime registry
// identity, which is all it has. The two spaces share one 64-bit field, so the
// top bit distinguishes them: RegistryId is a pair of 16-bit values, leaving the
// packed form far below it, and a minted ZoneId is not expected to reach it.
[[nodiscard]] inline std::uint64_t EditorRenderScope(const Registry& registry)
{
    constexpr std::uint64_t kRuntimeRegistrySpace = std::uint64_t{ 1 } << 63;

    if (registry.Kind == RegistryKind::Zone)
    {
        assert((registry.Zone.Value & kRuntimeRegistrySpace) == 0
               && "zone id uses the bit reserved to separate document scopes");
        return registry.Zone.Value;
    }

    return kRuntimeRegistrySpace
        | (static_cast<std::uint64_t>(registry.Id.Index) << 16)
        | static_cast<std::uint64_t>(registry.Id.Generation);
}

[[nodiscard]] inline RenderEntityKey MakeRenderEntityKey(
    const Registry& registry, EntityId entity)
{
    return RenderEntityKey{ EditorRenderScope(registry), entity };
}
