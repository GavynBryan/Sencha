#pragma once

#include "properties/PropertySource.h"

class AnimationPreviewWorkspace;

// The Root motion lens's card: whether the scenario moves the character, its
// walls (both scenario state), and carried against achieved travel.
class AnimationRootMotionProperties final : public CommitOnlyPropertySource
{
public:
    explicit AnimationRootMotionProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    AnimationPreviewWorkspace& Workspace;
};
