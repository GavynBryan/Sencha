#pragma once

#include "ui/IEditorPanel.h"

class AnimationPreviewWorkspace;

class AnimationRequestSchemaPanel final : public IEditorPanel
{
public:
    explicit AnimationRequestSchemaPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Request schema authoring"; }
    PanelPersistence GetPersistence() const override { return {"animation.request_schema"}; }
    DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }
    void OnDraw() override;
private:
    AnimationPreviewWorkspace& Workspace;
};
