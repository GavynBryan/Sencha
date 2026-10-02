#pragma once

#include "properties/PropertySource.h"

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationRigReportProperties
//
// What a game's `anim.risk` would say of the open rig -- counts that show it
// growing toward a graph, none an error, and the findings behind them -- and
// what the previewed entity carries: its animation state's size, and the
// requests that ended with no layer playing them.
//=============================================================================
class AnimationRigReportProperties final : public IPropertySource
{
public:
    explicit AnimationRigReportProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view) override { return false; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Gone; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Gone; }
    void CancelEdit(std::string_view) override {}

private:
    AnimationPreviewWorkspace& Workspace;
};
