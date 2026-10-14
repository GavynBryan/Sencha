#pragma once

#include "properties/PropertySource.h"

#include <string>

class AnimationPreviewWorkspace;

// What the stage shows when nothing on it is picked: the content auditioned
// and why a choice was refused. Read-only: choosing is the Content explorer's.
class AnimationAuditionProperties final : public ReadOnlyPropertySource
{
public:
    explicit AnimationAuditionProperties(const AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;

private:
    const AnimationPreviewWorkspace& Workspace;
};

// The open rig's scenario: its name, file, participants and fixture tags (an
// edit replays it), and what the rig depends on.
class AnimationScenarioProperties final : public CommitOnlyPropertySource
{
public:
    explicit AnimationScenarioProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    AnimationPreviewWorkspace& Workspace;
    // Why the last tag would not declare, until one does.
    std::string TagError;
};
