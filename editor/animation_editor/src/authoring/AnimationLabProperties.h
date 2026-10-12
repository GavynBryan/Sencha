#pragma once

#include "properties/PropertySource.h"

#include <anim/AnimTypes.h>

#include <optional>
#include <string>

class AnimationPreviewWorkspace;

// The session lab as a sheet: the working scenario run on an authority and on
// a client that gets its requests only over a link (when it joins, how late
// and lossy the link is, the client's guesses), then whether the two agreed and
// from when, and both machines side by side on the tick the deck inspects.
class AnimationLabProperties final : public IPropertySource
{
public:
    explicit AnimationLabProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    [[nodiscard]] std::vector<PropertyNode> RunNodes() const;

    AnimationPreviewWorkspace& Workspace;
    std::string Problem;
};
