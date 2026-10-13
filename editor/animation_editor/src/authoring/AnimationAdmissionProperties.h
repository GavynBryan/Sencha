#pragma once

#include "properties/PropertySource.h"

class AnimationPreviewWorkspace;

// What the simulation's events met: the role it plays, the verbs a recorder
// stands behind, and what their bindings answered (role and recorders are
// scenario state).
class AnimationAdmissionProperties final : public CommitOnlyPropertySource
{
public:
    explicit AnimationAdmissionProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;

private:
    AnimationPreviewWorkspace& Workspace;
};
