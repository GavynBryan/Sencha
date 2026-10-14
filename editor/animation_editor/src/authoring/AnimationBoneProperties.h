#pragma once

#include "properties/PropertySource.h"

class AnimationPreviewWorkspace;

// The joint picked on the stage: its parent, the layers covering it, and the
// mask steps that add or remove it, each an edit of the rig document.
class AnimationBoneProperties final : public ReadOnlyPropertySource
{
public:
    explicit AnimationBoneProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    AnimationPreviewWorkspace& Workspace;
};
