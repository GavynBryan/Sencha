#pragma once

#include "properties/PropertySource.h"
#include "workspaces/WorkspaceFacets.h"

#include <anim/AnimTypes.h>

#include <optional>
#include <string>

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationLabProperties
//
// The session lab as a sheet: the working scenario run on an authority and on
// a client that gets the authority's requests only over a link -- when it
// joins, how late and lossy the link is, and the client's guesses ahead of the
// authority -- then whether the two agreed and from when, and both machines
// side by side on a tick. While it is up the deck runs the lab's own clock.
//=============================================================================
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

    // The lab's clock, once it has run.
    [[nodiscard]] std::optional<WorkspaceTimelineFacet> Timeline() const;
    void InspectAt(double seconds);
    void InspectTick(AnimTick tick);

private:
    [[nodiscard]] std::vector<PropertyNode> RunNodes() const;

    AnimationPreviewWorkspace& Workspace;
    AnimTick Inspected = 0;
    std::string Problem;
};
