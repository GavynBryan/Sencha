#pragma once

#include <imgui.h>

class WorkspaceHost;
struct WorkspaceBarControls;

// The tab strip under the caption: a tab per open workspace, a way to open the
// rest, and the active one's controls. Its clicks are requests the host
// applies at the frame boundary. Draw reserves its own height of work area.
class WorkspaceBar
{
public:
    explicit WorkspaceBar(WorkspaceHost& host)
        : Host(host)
    {
    }

    void Draw(const WorkspaceBarControls* controls);
    [[nodiscard]] static float RowHeight();

private:
    void DrawRow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const WorkspaceBarControls* controls);
    void DrawTabs(float buttonSize, float right);

    WorkspaceHost& Host;
};
