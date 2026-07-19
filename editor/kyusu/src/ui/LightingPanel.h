#pragma once

#include "ui/IEditorPanel.h"

#include <ecs/EntityId.h>

#include <cstdint>
#include <functional>

class CommandStack;
class SelectionService;
struct ShadowResidencyReadout;

// Shadow budget readout over the render feature's per-frame residency
// snapshot: requested vs shadowed counts, a persistent warning row naming
// denied lights (click to select), per-light rows with tier, policy, slot,
// and age, selected-light cost, and the spot atlas map. Values only; every
// number comes from the snapshot, and the editor runs the game's arbiter
// with the game's budget cvars, so the readout predicts the game's grants.
class LightingPanel : public IEditorPanel
{
public:
    // The viewport baked-lighting preview, as the panel sees it.
    enum class BakedPreviewState : std::uint8_t
    {
        Unavailable, // nothing cooked yet this session
        Off,
        Fresh,       // showing the last cook, document unchanged since
        Stale,       // showing the last cook, document edited since (recook)
    };

    LightingPanel(const ShadowResidencyReadout& readout,
                  SelectionService& selection,
                  CommandStack& commands,
                  std::function<void()> invalidateShadows,
                  std::function<std::uint32_t()> countBakedDirectLights,
                  std::function<BakedPreviewState()> bakedPreviewState = {},
                  std::function<void(bool)> setBakedPreview = {});

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }
    // Tabs with the inspector (same slot, same group) so diagnostics do not
    // permanently shrink it.
    int GetDockTabGroup() const override { return 0; }

private:
    void DrawBudgetHeader();
    void DrawDeniedWarning();
    void DrawLightRows();
    void DrawSelectedCostLine();
    void DrawAtlasMap();
    void SelectLight(EntityId entity);

    const ShadowResidencyReadout& Readout;
    SelectionService& Selection;
    CommandStack& Commands;
    std::function<void()> InvalidateShadows;
    std::function<std::uint32_t()> CountBakedDirectLights;
    std::function<BakedPreviewState()> BakedPreview;
    std::function<void(bool)> SetBakedPreview;
};
