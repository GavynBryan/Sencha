#pragma once

#include "ui/IEditorPanel.h"

#include <vector>

class DataEditorWorkspace;
class MovementResolvePreview;

// Translates coefficients into the outcomes a designer judges: how high and how
// long the jump is, how fast the character gets up to speed, how far it slides
// to a stop. Recomputed from the profile while a value is being dragged.
class MovementResponsePanel final : public IEditorPanel
{
public:
    MovementResponsePanel(DataEditorWorkspace& workspace, MovementResolvePreview& preview);
    [[nodiscard]] std::string_view GetTitle() const override { return "Response"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::CenterBottom; }
    [[nodiscard]] int GetDockTabGroup() const override { return 0; }
    void OnDraw() override;

private:
    DataEditorWorkspace& Workspace;
    MovementResolvePreview& Preview;

    // Reused between frames so a redraw does not reallocate the plot buffers.
    std::vector<float> HeightPlot;
    std::vector<float> SpeedPlot;
};
