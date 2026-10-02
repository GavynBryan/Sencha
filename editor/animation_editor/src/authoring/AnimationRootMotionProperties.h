#pragma once

#include "properties/PropertySource.h"

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationRootMotionProperties
//
// The Root motion lens's card: whether the scenario moves the character, the
// walls it may run into, what the base layer's clip carries and where along
// its root curve it is, and how far it was carried against how far it got.
// The walls and the switch are the scenario's, saved with it.
//=============================================================================
class AnimationRootMotionProperties final : public IPropertySource
{
public:
    explicit AnimationRootMotionProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    AnimationPreviewWorkspace& Workspace;
};
