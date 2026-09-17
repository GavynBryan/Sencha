#pragma once

#include "authoring/DocumentLibrary.h"
#include "authoring/UiPreviewSession.h"

class SourceReloadRoots;

//=============================================================================
// ShojiStatusBar
//
// The bottom plate: the open document, its surface size and scale, how many
// roots are watched, and the diagnostics count with a lamp that goes to alert
// on the first error.
//=============================================================================
class ShojiStatusBar
{
public:
    ShojiStatusBar(UiPreviewSession& session, DocumentLibrary& library, SourceReloadRoots& watch);
    void Draw();

private:
    UiPreviewSession& Session;
    DocumentLibrary& Library;
    SourceReloadRoots& Watch;
};
