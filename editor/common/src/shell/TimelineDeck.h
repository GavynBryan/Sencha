#pragma once

#include "ui/UiScreenSlot.h"

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
class IWorkspaceTimeline;
class UiService;
class WorkspaceHost;
struct UiAction;
struct WorkspaceTimelineFacet;

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
    // Raised by `pixels` above where it sits, to stay clear of what lies along
    // the bottom of the window under it.
    void Lift(float pixels);
    [[nodiscard]] bool IsShowing() const { return Screen.IsOpen(); }
    // Where the deck sits in the window, while it shows.
    [[nodiscard]] std::optional<UiElementBox> Area() const;

private:
    void Open();
    void Close();
    void HandleActions(IWorkspaceTimeline& timeline, const WorkspaceTimelineFacet& shown);
    void Publish(const WorkspaceTimelineFacet& timeline);
    // A pointer x on the ruler, in surface pixels, as the clock's time.
    [[nodiscard]] std::optional<double> TimeAt(const WorkspaceTimelineFacet& timeline, double x) const;

    UiService& Ui;
    WorkspaceHost& Workspaces;
    ActionContextStack& Actions;
    PresentationId Presentation;
    UiScreenSlot Screen;
    std::size_t LaneCount = 0;
};
