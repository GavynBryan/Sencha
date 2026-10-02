#pragma once

#include "authoring/AnimationScenarioBatch.h"

#include "properties/PropertySource.h"

#include <vector>

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationScenarioBatchProperties
//
// Every saved scenario in the project run twice from the start, each in a
// session of its own: a verdict per scenario, what it ended on, and why it
// warned or failed, with the scenario's rig a verb away.
//=============================================================================
class AnimationScenarioBatchProperties final : public IPropertySource
{
public:
    explicit AnimationScenarioBatchProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view) override { return false; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Gone; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Gone; }
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    AnimationPreviewWorkspace& Workspace;
    std::vector<AnimationScenarioRun> Runs;
};
