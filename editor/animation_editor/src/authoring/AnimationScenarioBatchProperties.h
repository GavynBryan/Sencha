#pragma once

#include "authoring/AnimationScenarioBatch.h"

#include "properties/PropertySource.h"

#include <functional>
#include <string>
#include <vector>

class AnimationPreviewWorkspace;

// Every saved scenario in the project run twice from the start, each in a
// session of its own: a verdict per scenario, what it ended on, and why it
// warned or failed, with the scenario's rig opened through `openRig`.
class AnimationScenarioBatchProperties final : public IPropertySource
{
public:
    AnimationScenarioBatchProperties(AnimationPreviewWorkspace& workspace,
                                     std::function<void(const std::string& rig)> openRig)
        : Workspace(workspace)
        , OpenRig(std::move(openRig))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view) override { return false; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Gone; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Gone; }
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    AnimationPreviewWorkspace& Workspace;
    std::function<void(const std::string& rig)> OpenRig;
    std::vector<AnimationScenarioRun> Runs;
};
