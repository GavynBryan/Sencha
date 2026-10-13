#pragma once

#include "properties/PropertySource.h"

#include <string>

class AnimationPreviewWorkspace;

// The Blends lens's card: how each layer blends now, the selected layer's
// blendspace and the latest blends; its details are the overrides and A/B takes.
class AnimationBlendProperties final : public CommitOnlyPropertySource
{
public:
    explicit AnimationBlendProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;

    [[nodiscard]] IPropertySource& Comparison() { return Takes; }

private:
    class TakeSource final : public CommitOnlyPropertySource
    {
    public:
        explicit TakeSource(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
        [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
        PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
        PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    private:
        AnimationPreviewWorkspace& Workspace;
    };

    AnimationPreviewWorkspace& Workspace;
    TakeSource Takes{ Workspace };
};
