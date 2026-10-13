#pragma once

#include "properties/PropertySource.h"

#include <anim/AnimTypes.h>

#include <optional>
#include <string>

class AnimationPreviewWorkspace;

// The session lab as a sheet: the scenario run on an authority and on a client
// linked as it says, whether they agreed, and both on the deck's tick.
class AnimationLabProperties final : public CommitOnlyPropertySource
{
public:
    explicit AnimationLabProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    [[nodiscard]] std::vector<PropertyNode> RunNodes() const;

    AnimationPreviewWorkspace& Workspace;
    std::string Problem;
};
