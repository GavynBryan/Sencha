#pragma once

#include "properties/PropertySource.h"

#include <string>

class AnimationPreviewWorkspace;

// The Layers lens's card: each layer's mode, weight, mask and what it plays,
// muted or soloed on the stage, and an inspected one's weight source and mask.
class AnimationLayerProperties final : public IPropertySource
{
public:
    explicit AnimationLayerProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override { return key == "muted" || key == "soloed"; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    [[nodiscard]] std::string Title() const;
    // Back from one layer to all of them; false when there was nowhere to go.
    bool StepBack();

private:
    [[nodiscard]] std::vector<PropertyNode> LayerNodes() const;

    AnimationPreviewWorkspace& Workspace;
    bool Inspecting = false;
};
