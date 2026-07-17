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
// light, both slot tables, frame counters, and the budgets in force. Plain
// values only, so the panel never touches the renderer.
//=============================================================================
struct ShadowResidencyReadout
{
    struct LightRow
    {
        EntityId Entity;
        GpuLightType Type = GpuLightType::Spot;
        float Score = 0.0f;
        // Requested tier (tile edge in texels); the granted tile may be
        // smaller after downgrade (see the slot's Allocation).
        std::uint32_t TileSize = 0;
        ShadowUpdatePolicy Policy = ShadowUpdatePolicy::OnChange;
        bool Held = false;
        std::uint32_t Slot = UINT32_MAX;
    };

    // False until the WYSIWYG path has at least one shadow target.
    bool Active = false;
    // Focus-scene registry, for building selection refs from row entities.
    RegistryId FocusRegistry;
    std::vector<LightRow> Rows;
    ShadowFrameStats Stats;
    SpotShadowSlotInfo Slots[kMaxSpotShadows];
    PointShadowSlotInfo PointSlots[kMaxPointShadows];
    ShadowResidencyBudgets Budgets;
};
