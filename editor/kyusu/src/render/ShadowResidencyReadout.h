#pragma once

#include <ecs/EntityId.h>
#include <render/ShadowResidency.h>
#include <world/registry/RegistryId.h>

#include <cstdint>
#include <vector>

//=============================================================================
// ShadowResidencyReadout
//
// Per-frame snapshot of the editor's shadow arbitration, published by
// EditorRenderFeature for the lighting panel: one row per shadow-requesting
// spot light in request (priority) order, the arbiter's slot table and frame
// counters, and the budgets in force. Plain values only, so the panel never
// touches the renderer.
//=============================================================================
struct ShadowResidencyReadout
{
    struct LightRow
    {
        EntityId Entity;
        float Score = 0.0f;
        // Requested tier (tile edge in texels); the granted tile may be
        // smaller after downgrade (see the slot's Allocation).
        std::uint32_t TileSize = 0;
        ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
        bool Held = false;
        std::uint32_t Slot = UINT32_MAX;
    };

    // False until the WYSIWYG path and the shadow atlas are up.
    bool Active = false;
    // Focus-scene registry, for building selection refs from row entities.
    RegistryId FocusRegistry;
    std::vector<LightRow> Rows;
    SpotShadowFrameStats Stats;
    SpotShadowSlotInfo Slots[kMaxSpotShadows];
    ShadowResidencyBudgets Budgets;
};
