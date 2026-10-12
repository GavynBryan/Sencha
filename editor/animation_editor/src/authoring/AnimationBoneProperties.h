#pragma once

#include "properties/PropertySource.h"

class AnimationPreviewWorkspace;

// The joint picked on the stage: its parent, the layers covering it, and the
// mask steps that add or remove it, each an edit of the rig document.
class AnimationBoneProperties final : public IPropertySource
{
public:
    explicit AnimationBoneProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view) override { return false; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    AnimationPreviewWorkspace& Workspace;
};
