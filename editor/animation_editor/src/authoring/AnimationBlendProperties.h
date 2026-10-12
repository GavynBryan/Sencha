#pragma once

#include "properties/PropertySource.h"

#include <string>

class AnimationPreviewWorkspace;

// The Blends lens's card: how each layer blends now, the selected layer's
// blendspace and the latest blends; its details are the overrides and A/B takes.
class AnimationBlendProperties final : public IPropertySource
{
public:
    explicit AnimationBlendProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override { return key == "layer"; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}

    [[nodiscard]] IPropertySource& Comparison() { return Takes; }

private:
    class TakeSource final : public IPropertySource
    {
    public:
        explicit TakeSource(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
        [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
        bool BeginEdit(std::string_view key) override { return key == "ab.ghost"; }
        PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
        PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
        void CancelEdit(std::string_view) override {}
        PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    private:
        AnimationPreviewWorkspace& Workspace;
    };

    AnimationPreviewWorkspace& Workspace;
    TakeSource Takes{ Workspace };
};
