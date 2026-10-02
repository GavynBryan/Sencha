#pragma once

#include <graphics/PresentationId.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

class ActionContextStack;
struct TimelineSample;
class IWorkspace;
class UiService;
class WorkspaceHost;
struct UiAction;
struct WorkspaceTimelineFacet;

// A plotted lane as `bars` columns from `start` to `end`: each the largest
// sample in its stretch, as a share of the largest of all.
[[nodiscard]] std::vector<double> TimelinePlotPeaks(std::span<const TimelineSample> samples, double start, double end,
                                                    std::size_t bars);

//=============================================================================
// TimelineDeck
//
// The active workspace's clock along the bottom edge of the window, when it
// keeps one: which clock and a switch between them, the transport as keys,
// the readout, and a ruler with the playhead and the marks. Dragging the ruler
// scrubs, dragging a movable mark moves it, clicking one picks it; all of it
// goes back to the workspace, which owns the time.
//=============================================================================
class TimelineDeck
{
public:
    TimelineDeck(UiService& ui, UiSurfaceId surface, WorkspaceHost& workspaces, ActionContextStack& actions,
                 PresentationId presentation);
    ~TimelineDeck();

    TimelineDeck(const TimelineDeck&) = delete;
    TimelineDeck& operator=(const TimelineDeck&) = delete;

    // Per frame: act on what was done to the deck, then show the clock now.
    void Update();
    // While a question stands over the window.
    void Hide() { Close(); }
    [[nodiscard]] bool IsShowing() const { return Screen.IsValid(); }
    // The lanes under its ruler; what rises above the deck clears them too.
    [[nodiscard]] std::size_t Lanes() const { return LaneCount; }

private:
    void Open();
    void Close();
    void HandleActions(IWorkspace& workspace, const WorkspaceTimelineFacet& timeline);
    void Publish(const WorkspaceTimelineFacet& timeline);
    // A pointer x on the ruler, in surface pixels, as the clock's time.
    [[nodiscard]] std::optional<double> TimeAt(const WorkspaceTimelineFacet& timeline, double x) const;

    UiService& Ui;
    UiSurfaceId Surface;
    WorkspaceHost& Workspaces;
    ActionContextStack& Actions;
    PresentationId Presentation;
    UiScreenHandle Screen;
    std::size_t LaneCount = 0;
};
