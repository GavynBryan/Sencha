#pragma once

#include "properties/PropertySource.h"

#include <functional>
#include <string>

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationFlowProperties
//
// The Flow lens's card: the flow a layer plays on the inspected tick, the
// section it is in and how far, where a cancel goes, and how each section is
// left -- loop, branches, the end. Edit opens the flow as a sheet.
//=============================================================================
class AnimationFlowProperties final : public IPropertySource
{
public:
    // `open` brings the flow's document up on the card.
    AnimationFlowProperties(AnimationPreviewWorkspace& workspace, std::function<void(const std::string&)> open)
        : Workspace(workspace)
        , Open(std::move(open))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override { return key == "layer"; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    [[nodiscard]] std::string Title() const;

private:
    AnimationPreviewWorkspace& Workspace;
    std::function<void(const std::string&)> Open;
};
