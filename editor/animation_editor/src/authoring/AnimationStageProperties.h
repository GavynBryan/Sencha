#pragma once

#include "properties/PropertySource.h"

#include <string>

class AnimationPreviewWorkspace;

// What the stage shows when nothing on it is picked: the content auditioned
// and why a choice was refused. Read-only: choosing is the Content explorer's.
class AnimationAuditionProperties final : public IPropertySource
{
public:
    explicit AnimationAuditionProperties(const AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view) override { return false; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    void CancelEdit(std::string_view) override {}

private:
    const AnimationPreviewWorkspace& Workspace;
};

// The open rig's scenario: its name, file, participants and fixture tags (an
// edit replays it), and what the rig depends on.
class AnimationScenarioProperties final : public IPropertySource
{
public:
    explicit AnimationScenarioProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    AnimationPreviewWorkspace& Workspace;
    // Why the last tag would not declare, until one does.
    std::string TagError;
};
