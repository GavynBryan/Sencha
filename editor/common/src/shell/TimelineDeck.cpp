#include "shell/TimelineDeck.h"

#include "actions/ActionContext.h"
#include "workspaces/IWorkspace.h"
#include "workspaces/WorkspaceHost.h"

#include <ui/UiService.h>

#include <algorithm>
#include <array>
#include <format>
#include <vector>

namespace
{
const UiModelPropertyId kClock = UiPropertyIdAt(0);
const UiModelPropertyId kReadout = UiPropertyIdAt(1);
const UiModelPropertyId kPlaying = UiPropertyIdAt(2);
const UiModelPropertyId kLive = UiPropertyIdAt(3);
const UiModelPropertyId kPlayhead = UiPropertyIdAt(4);
const UiModelRowsId kKeys = UiRowsIdAt(0);
const UiModelRowsId kMarks = UiRowsIdAt(1);

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
    };
    desc.RowLists = { "keys", "marks" };
    desc.Actions = { "deck_invoke", "deck_scrub", "deck_mark", "deck_pick" };
    return desc;
}

std::string Percent(double fraction)
{
    return std::format("{:.3f}%", std::clamp(fraction, 0.0, 1.0) * 100.0);
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

std::string Text(const UiAction& action, std::size_t index)
{
    return index < action.Arguments.size() && action.Arguments[index].Kind() == UiValueKind::String
               ? std::string(action.Arguments[index].AsString())
               : std::string{};
}

double Number(const UiAction& action, std::size_t index)
{
    if (index >= action.Arguments.size())
        return 0.0;
    const UiValue& value = action.Arguments[index];
    return value.Kind() == UiValueKind::Float ? value.AsFloat()
           : value.Kind() == UiValueKind::Int ? static_cast<double>(value.AsInt())
                                               : 0.0;
}
} // namespace

TimelineDeck::TimelineDeck(UiService& ui, UiSurfaceId surface, WorkspaceHost& workspaces,
                           ActionContextStack& actions, PresentationId presentation)
    : Ui(ui)
    , Surface(surface)
    , Workspaces(workspaces)
    , Actions(actions)
    , Presentation(presentation)
{
}

TimelineDeck::~TimelineDeck()
{
    Close();
}

void TimelineDeck::Update()
{
    IWorkspace* workspace = Workspaces.ActiveIn(Presentation);
    const WorkspaceTimelineFacet timeline = workspace != nullptr ? workspace->Timeline() : WorkspaceTimelineFacet{};
    if (timeline.Clock.empty())
    {
        Close();
        return;
    }
    Open();
    HandleActions(*workspace, timeline);
    Publish(workspace->Timeline());
}

void TimelineDeck::Open()
{
    if (!Screen.IsValid() && Surface.IsValid())
        Screen = Ui.OpenScreen(Surface, Describe());
}

void TimelineDeck::Close()
{
    if (!Screen.IsValid())
        return;
    Ui.CloseScreen(Screen);
    Screen = {};
}

void TimelineDeck::HandleActions(IWorkspace& workspace, const WorkspaceTimelineFacet& timeline)
{
    if (!Screen.IsValid())
        return;
    for (const UiAction& action : Ui.DrainActions(Screen))
    {
        if (action.Id == kInvokeAction)
            (void)Actions.Invoke(Text(action, 0));
        else if (action.Id == kScrubAction)
        {
            const std::optional<TimelineDrag> phase = DragPhase(Text(action, 0));
            if (const std::optional<double> time = TimeAt(timeline, Number(action, 1)); phase && time)
                workspace.Scrub(*time, *phase);
        }
        else if (action.Id == kMarkAction)
        {
            const std::optional<TimelineDrag> phase = DragPhase(Text(action, 0));
            if (const std::optional<double> time = TimeAt(timeline, Number(action, 2)); phase && time)
                workspace.MoveMark(Text(action, 1), *time, *phase);
        }
        else if (action.Id == kPickAction)
            workspace.PickMark(Text(action, 0));
    }
}

std::optional<double> TimelineDeck::TimeAt(const WorkspaceTimelineFacet& timeline, double x) const
{
    const std::optional<UiElementBox> ruler = Ui.MeasureElement(Screen, "ruler");
    if (!ruler || ruler->Width <= 0.0f)
        return std::nullopt;
    const double fraction = std::clamp((x - ruler->X) / ruler->Width, 0.0, 1.0);
    return timeline.Start + fraction * (timeline.End - timeline.Start);
}

void TimelineDeck::Publish(const WorkspaceTimelineFacet& timeline)
{
    if (!Screen.IsValid())
        return;
    const double span = timeline.End - timeline.Start;
    const auto at = [&](double time) { return span > 0.0 ? (time - timeline.Start) / span : 0.0; };
    (void)Ui.SetValue(Screen, kClock, UiValue(timeline.Clock));
    (void)Ui.SetValue(Screen, kReadout, UiValue(timeline.Readout));
    (void)Ui.SetValue(Screen, kPlaying, UiValue(timeline.Playing));
    (void)Ui.SetValue(Screen, kLive, UiValue(timeline.Live));
    (void)Ui.SetValue(Screen, kPlayhead, UiValue(Percent(at(timeline.Time))));

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
    (void)Ui.SetRows(Screen, kKeys, keys);

    std::vector<UiRow> marks;
    for (const TimelineMark& mark : timeline.Marks)
    {
        UiRow row{ mark.Label, mark.Movable ? "movable" : "",
                   mark.Shape == TimelineMarkShape::Diamond ? "diamond" : "round" };
        row.Key = mark.Key;
        row.Selected = mark.Selected;
        row.Parts = { Percent(at(mark.Time)) };
        marks.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen, kMarks, marks);
}
