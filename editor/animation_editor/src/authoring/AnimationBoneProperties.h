#pragma once

#include "properties/PropertySource.h"

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationBoneProperties
//
// The joint picked on the stage: its parent, which of the rig's layers cover
// it, and the steps that put it into or take it out of the selected layer's
// mask, with or without everything below it. A step is an edit of the rig
// document, in its transaction.
//=============================================================================
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
