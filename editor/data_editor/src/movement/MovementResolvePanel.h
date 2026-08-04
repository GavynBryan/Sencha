#pragma once

#include "ui/IEditorPanel.h"

class DataEditorWorkspace;
class MovementResolvePreview;

// Answers "why is this coefficient that value?" for a movement profile: dial in
// the facts a character would have, see which rules go live, and read where
// each final number came from.
class MovementResolvePanel final : public IEditorPanel
{
public:
    MovementResolvePanel(DataEditorWorkspace& workspace, MovementResolvePreview& preview);
    [[nodiscard]] std::string_view GetTitle() const override { return "Resolve"; }
    // The lower right column: the effective-value table is tall and needs the
    // full width of that column, which packing beside Documentation denies it.
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }
    void OnDraw() override;

private:
    DataEditorWorkspace& Workspace;
    MovementResolvePreview& Preview;
};
