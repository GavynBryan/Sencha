#pragma once

#include "properties/PropertySource.h"

class AnimationPreviewWorkspace;

// What a game's `anim.risk` says of the open rig, and the size of what the
// previewed entity carries.
class AnimationRigReportProperties final : public ReadOnlyPropertySource
{
public:
    explicit AnimationRigReportProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;

private:
    AnimationPreviewWorkspace& Workspace;
};
