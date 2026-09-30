#pragma once

#include <graphics/PresentationId.h>

#include <imgui.h>

class WorkspaceHost;
struct WorkspaceBarControls;

// The tab strip under a window's caption: a tab per workspace placed in that
// window, the active one's controls, and in the main window a way to open the
// rest. A tab dragged down off the strip, or moved from its menu, goes to a
// window of its own; a detached tab's menu brings it back. Its clicks are
// requests the host applies at the frame boundary. Draw reserves its own
// height of work area.
class WorkspaceBar
{
public:
    WorkspaceBar(WorkspaceHost& host, PresentationId window)
        : Host(host)
        , Window(window)
    {
    }

    void Draw(const WorkspaceBarControls* controls);
    [[nodiscard]] static float RowHeight();

private:
    void DrawRow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const WorkspaceBarControls* controls);
    void DrawTabs(float buttonSize, float right);

    WorkspaceHost& Host;
    PresentationId Window;
};
