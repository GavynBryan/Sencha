#pragma once

#include <graphics/PresentationId.h>

#include <string>
#include <vector>

class WorkspaceHost;

// One tab on a window's rail: an asset open in an editor, or an editor with
// nothing open, which is a tab of its own.
struct AssetTab
{
    std::string Kind;
    std::string KindLabel;
    // The asset's key in its editor; empty for an editor's own tab.
    std::string Document;
    std::string Label;
    bool Changed = false;
    // Its editor is the one showing, and it is the asset that editor shows.
    bool Current = false;
};

// Every asset open in an editor placed in `window`, editor by editor in the
// host's order and each editor's assets in its own.
[[nodiscard]] std::vector<AssetTab> AssetTabsIn(const WorkspaceHost& host, PresentationId window);
