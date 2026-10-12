#pragma once

#include "properties/PropertySource.h"

class AnimationPreviewWorkspace;

// What the simulation's events met: the role it plays, the verbs a recorder
// stands behind, and what their bindings answered (role and recorders are
// scenario state).
class AnimationAdmissionProperties final : public IPropertySource
{
public:
    explicit AnimationAdmissionProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}

private:
    AnimationPreviewWorkspace& Workspace;
};
