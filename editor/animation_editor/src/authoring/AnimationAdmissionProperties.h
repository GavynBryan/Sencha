#pragma once

#include "properties/PropertySource.h"

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationAdmissionProperties
//
// What the simulation's events met: whether the preview plays the authority or
// a client, which verbs a recorder stands behind (nothing runs the game here,
// so any other verb answers Unavailable), and the latest event crossings and
// behavior lifecycle events with what their bindings answered, and what the
// recorders were handed. Role and recorders are scenario state.
//=============================================================================
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
