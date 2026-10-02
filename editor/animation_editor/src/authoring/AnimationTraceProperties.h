#pragma once

#include "authoring/AnimationTraceImport.h"

#include "properties/PropertySource.h"

#include <optional>
#include <string>

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationTraceProperties
//
// A trace a running game wrote with anim.trace.export, read back: whose it is
// and which rig it ran, what it did not capture, and its decision records as
// logged, filtered by cause, layer or detail. There is no pose history in it
// to scrub.
//=============================================================================
class AnimationTraceProperties final : public IPropertySource
{
public:
    explicit AnimationTraceProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override { return key == "file" || key == "filter"; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    AnimationPreviewWorkspace& Workspace;
    std::string File;
    std::string Filter;
    std::string Error;
    std::optional<AnimationTrace> Trace;
};
