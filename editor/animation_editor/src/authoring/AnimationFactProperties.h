#pragma once

#include "properties/PropertySource.h"

#include <string>

class AnimationPreviewWorkspace;

// The Facts lens's card: every fact the rig reads, now and on the next tick;
// an input fact is driven or cleared as a scenario edit, and one is plotted.
class AnimationFactProperties final : public CommitOnlyPropertySource
{
public:
    explicit AnimationFactProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    // The fact plotted along the deck, or empty.
    [[nodiscard]] const std::string& Plotted() const { return Plot; }

private:
    AnimationPreviewWorkspace& Workspace;
    std::string Plot;
};
