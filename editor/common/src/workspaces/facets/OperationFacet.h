#pragma once

#include "icons/IconId.h"
#include "tools/ToolDraft.h"

#include <string>
#include <vector>

class IPropertySource;

// An operation the workspace offers, and the action that puts it in hand.
struct OperationChoice
{
    std::string Label;
    IconId Icon = IconId::None;
    std::string Action;
    bool Current = false;
};

// What the workspace's hands are doing: the tool, or mode, in effect, what it is
// set to, and whether it has drafted work. Apply and Cancel are the actions
// tool.apply and tool.cancel, which a workspace with operations claims.
struct WorkspaceOperationFacet
{
    // Empty when the workspace has no operations to show.
    std::string Name = {};
    // The sub-mode in effect, or empty.
    std::string Variant = {};
    IconId Icon = IconId::None;
    ToolDraft Draft = ToolDraft::None;
    // What to do next, or why the draft cannot apply.
    std::string Hint = {};
    // What the operation is set to; null when it has no settings. Owned by the
    // workspace, valid until its next frame.
    IPropertySource* Parameters = nullptr;
    // Every operation the workspace offers, in its own order.
    std::vector<OperationChoice> Choices = {};
};
