#pragma once

#include "properties/PropertySource.h"

#include <functional>
#include <string>

class AnimationPreviewWorkspace;

// The Flow lens's card: the flow a layer plays on the inspected tick, where it
// is, where a cancel goes and how each section is left.
class AnimationFlowProperties final : public CommitOnlyPropertySource
{
public:
    // `open` brings the flow's document up on the card.
    AnimationFlowProperties(AnimationPreviewWorkspace& workspace, std::function<void(const std::string&)> open)
        : Workspace(workspace)
        , Open(std::move(open))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    [[nodiscard]] std::string Title() const;

private:
    AnimationPreviewWorkspace& Workspace;
    std::function<void(const std::string&)> Open;
};
