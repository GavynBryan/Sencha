#include "shell/TimelineDeck.h"

#include "actions/ActionContext.h"
#include "presenters/PlotPeaks.h"
#include "ui/DocumentLengths.h"
#include "workspaces/IWorkspace.h"
#include "workspaces/WorkspaceHost.h"
#include "workspaces/WorkspaceTimeline.h"

#include <ui/UiService.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>
#include <vector>

namespace
{
const UiModelPropertyId kClock = UiPropertyIdAt(0);
const UiModelPropertyId kReadout = UiPropertyIdAt(1);
const UiModelPropertyId kPlaying = UiPropertyIdAt(2);
const UiModelPropertyId kLive = UiPropertyIdAt(3);
const UiModelPropertyId kPlayhead = UiPropertyIdAt(4);
const UiModelPropertyId kTrackHeight = UiPropertyIdAt(5);
const UiModelPropertyId kLaned = UiPropertyIdAt(6);
const UiModelPropertyId kBottom = UiPropertyIdAt(7);
const UiModelRowsId kKeys = UiRowsIdAt(0);
const UiModelRowsId kMarks = UiRowsIdAt(1);
const UiModelRowsId kLanes = UiRowsIdAt(2);
const UiModelRowsId kSpans = UiRowsIdAt(3);
const UiModelRowsId kBars = UiRowsIdAt(4);

// The ruler's row, then one row per lane under it, in dp.
constexpr int kRulerHeight = 24;
constexpr int kLaneHeight = 16;
// The lanes the deck has room for; any past these are not shown.
constexpr std::size_t kLaneLimit = 4;
// Where it sits above the window's bottom edge, clear of the stations, in dp.
constexpr int kDeckBottomDp = 50;
// A plotted lane is drawn as this many bars, each the largest sample it covers.
constexpr std::size_t kPlotBars = 96;
constexpr double kBarHeight = 12.0;

const UiActionId kInvokeAction = UiActionIdAt(0);
const UiActionId kScrubAction = UiActionIdAt(1);
const UiActionId kMarkAction = UiActionIdAt(2);
const UiActionId kPickAction = UiActionIdAt(3);

// The transport, left to right: an action, what its key shows -- a word, or
// an icon-font glyph -- and, for play, what it shows while playing.
struct TransportKey
{
    std::string_view Action;
    std::string_view Label;
    bool Glyph = false;
    std::string_view Playing = {};
};
constexpr std::array<TransportKey, 11> kTransport{ {
    { "timeline.source.audition", "AUDITION" },
    { "timeline.source.simulation", "SIM" },
    { "timeline.restart", "\xEF\x81\x89", true },
    { "timeline.step_back", "\xEF\x81\x88", true },
    { "timeline.play", "\xEF\x81\x8B", true, "\xEF\x81\x8C" },
    { "timeline.step_forward", "\xEF\x81\x91", true },
    { "timeline.loop", "\xEF\x8D\xA3", true },
    { "timeline.slower", "\xEF\x81\xA8", true },
    { "timeline.faster", "\xEF\x81\xA7", true },
    { "timeline.run_ahead", "\xEF\x81\x90", true },
    { "timeline.live", "LIVE" },
} };

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://deck.rml";
    desc.ModelName = "deck";
    desc.Properties = {
        UiModelProperty{ "clock", UiValue(std::string{}) },
        UiModelProperty{ "readout", UiValue(std::string{}) },
        UiModelProperty{ "playing", UiValue(false) },
        UiModelProperty{ "live", UiValue(true) },
        UiModelProperty{ "playhead", UiValue(std::string("0%")) },
        UiModelProperty{ "track_height", UiValue(std::format("{}dp", kRulerHeight)) },
        UiModelProperty{ "laned", UiValue(false) },
        UiModelProperty{ "bottom", UiValue(std::format("{}dp", kDeckBottomDp)) },
    };
    desc.RowLists = { "keys", "marks", "lanes", "spans", "bars" };
    desc.Actions = { "deck_invoke", "deck_scrub", "deck_mark", "deck_pick" };
    return desc;
}

std::optional<TimelineDrag> DragPhase(std::string_view phase)
{
    if (phase == "begin")
        return TimelineDrag::Begin;
    if (phase == "move")
        return TimelineDrag::Move;
    if (phase == "end")
        return TimelineDrag::End;
    return std::nullopt;
}

} // namespace

TimelineDeck::TimelineDeck(UiService& ui, UiSurfaceId surface, WorkspaceHost& workspaces,
                           ActionContextStack& actions, PresentationId presentation)
    : Ui(ui)
    , Workspaces(workspaces)
    , Actions(actions)
    , Presentation(presentation)
    , Screen(ui, Describe, surface)
{
}

TimelineDeck::~TimelineDeck()
{
    Close();
}

void TimelineDeck::Update()
{
    IWorkspace* workspace = Workspaces.ActiveIn(Presentation);
    IWorkspaceTimeline* timeline = workspace != nullptr ? workspace->Timeline() : nullptr;
    const WorkspaceTimelineFacet shown = timeline != nullptr ? timeline->Facet() : WorkspaceTimelineFacet{};
    if (shown.Clock.empty())
    {
        Close();
        return;
    }
    Open();
    HandleActions(*timeline, shown);
    Publish(timeline->Facet());
}

void TimelineDeck::Open()
{
    (void)Screen.Open();
}

void TimelineDeck::Close()
{
    LaneCount = 0;
    Screen.Close();
}

void TimelineDeck::HandleActions(IWorkspaceTimeline& timeline, const WorkspaceTimelineFacet& shown)
{
    if (!Screen.IsOpen())
        return;
    for (const UiAction& action : Ui.DrainActions(Screen.Handle()))
    {
        if (action.Id == kInvokeAction)
            (void)Actions.Invoke(std::string(action.Arg(0).AsString()));
        else if (action.Id == kScrubAction)
        {
            const std::optional<TimelineDrag> phase = DragPhase(std::string(action.Arg(0).AsString()));
            if (const std::optional<double> time = TimeAt(shown, action.Arg(1).AsFloat()); phase && time)
                timeline.Scrub(*time, *phase);
        }
        else if (action.Id == kMarkAction)
        {
            const std::optional<TimelineDrag> phase = DragPhase(std::string(action.Arg(0).AsString()));
            if (const std::optional<double> time = TimeAt(shown, action.Arg(2).AsFloat()); phase && time)
                timeline.MoveMark(std::string(action.Arg(1).AsString()), *time, *phase);
        }
        else if (action.Id == kPickAction)
            timeline.Pick(std::string(action.Arg(0).AsString()));
    }
}

std::optional<double> TimelineDeck::TimeAt(const WorkspaceTimelineFacet& timeline, double x) const
{
    const std::optional<UiElementBox> ruler = Ui.MeasureElement(Screen.Handle(), "ruler");
    if (!ruler || ruler->Width <= 0.0f)
        return std::nullopt;
    const double fraction = std::clamp((x - ruler->X) / ruler->Width, 0.0, 1.0);
    return timeline.Start + fraction * (timeline.End - timeline.Start);
}

void TimelineDeck::Publish(const WorkspaceTimelineFacet& timeline)
{
    if (!Screen.IsOpen())
        return;
    const double span = timeline.End - timeline.Start;
    const auto at = [&](double time) { return span > 0.0 ? (time - timeline.Start) / span : 0.0; };
    (void)Ui.SetValue(Screen.Handle(), kClock, UiValue(timeline.Clock));
    (void)Ui.SetValue(Screen.Handle(), kReadout, UiValue(timeline.Readout));
    (void)Ui.SetValue(Screen.Handle(), kPlaying, UiValue(timeline.Playing));
    (void)Ui.SetValue(Screen.Handle(), kLive, UiValue(timeline.Live));
    (void)Ui.SetValue(Screen.Handle(), kPlayhead, UiValue(PercentLength(at(timeline.Time))));

    std::vector<UiRow> keys;
    for (const TransportKey& key : kTransport)
    {
        const ActionContextStack::State state = Actions.Query(key.Action);
        if (!state.Claimed)
            continue;
        const std::string_view label = timeline.Playing && !key.Playing.empty() ? key.Playing : key.Label;
        UiRow row{ std::string(label), "", key.Glyph ? "glyph" : "" };
        row.Key = key.Action;
        row.Selected = state.Checked;
        row.Disabled = !state.Enabled;
        keys.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen.Handle(), kKeys, keys);

    std::vector<UiRow> marks;
    for (const TimelineMark& mark : timeline.Marks)
    {
        UiRow row{ mark.Label, mark.Movable ? "movable" : "",
                   mark.Shape == TimelineMarkShape::Diamond ? "diamond" : "round" };
        row.Key = mark.Key;
        row.Selected = mark.Selected;
        row.Parts = { PercentLength(at(mark.Time)) };
        marks.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen.Handle(), kMarks, marks);

    std::vector<UiRow> lanes;
    std::vector<UiRow> spans;
    std::vector<UiRow> bars;
    for (std::size_t index = 0; const TimelineLane& lane : timeline.Lanes)
    {
        if (index == kLaneLimit)
            break;
        const int laneTop = kRulerHeight + kLaneHeight * static_cast<int>(index++);
        const std::string top = std::format("{}dp", laneTop);
        UiRow row{ lane.Label, "", "" };
        row.Parts = { top };
        lanes.push_back(std::move(row));
        for (const TimelineSpan& stretch : lane.Spans)
        {
            const double start = std::clamp(at(stretch.Start), 0.0, 1.0);
            UiRow piece{ stretch.Label, "", "" };
            piece.Key = stretch.Key;
            piece.Selected = stretch.Selected;
            piece.Parts = { PercentLength(start), PercentLength(std::clamp(at(stretch.End), start, 1.0) - start), top };
            spans.push_back(std::move(piece));
        }
        const std::vector<double> peaks = PlotPeaks(lane.Samples, timeline.Start, timeline.End, kPlotBars);
        for (std::size_t bar = 0; bar < peaks.size(); ++bar)
        {
            if (peaks[bar] <= 0.0)
                continue;
            const double height = std::max(1.0, kBarHeight * peaks[bar]);
            UiRow column{ "", "", "" };
            column.Parts = { PercentLength(static_cast<double>(bar) / kPlotBars), PercentLength(1.0 / kPlotBars),
                             std::format("{:.1f}dp", laneTop + 1 + kBarHeight - height),
                             std::format("{:.1f}dp", height) };
            bars.push_back(std::move(column));
        }
    }
    LaneCount = lanes.size();
    (void)Ui.SetRows(Screen.Handle(), kLanes, lanes);
    (void)Ui.SetRows(Screen.Handle(), kSpans, spans);
    (void)Ui.SetRows(Screen.Handle(), kBars, bars);
    (void)Ui.SetValue(Screen.Handle(), kLaned, UiValue(LaneCount > 0));
    (void)Ui.SetValue(Screen.Handle(), kTrackHeight,
                      UiValue(std::format("{}dp", kRulerHeight + kLaneHeight * static_cast<int>(LaneCount))));
}

void TimelineDeck::Lift(float pixels)
{
    if (Screen.IsOpen())
        (void)Ui.SetValue(Screen.Handle(), kBottom,
                          UiValue(PixelLength(pixels + kDeckBottomDp * Ui.GetSurfaceScale(Screen.Surface()))));
}

std::optional<UiElementBox> TimelineDeck::Area() const
{
    if (!Screen.IsOpen())
        return std::nullopt;
    return Ui.MeasureElement(Screen.Handle(), "deck", UiBoxArea::Border);
}
