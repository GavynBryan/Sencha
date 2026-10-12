#pragma once

#include "icons/IconId.h"
#include "tools/ToolDraft.h"
#include "workspaces/WorkspaceKind.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class IPropertySource;

// What a workspace tells the shell about itself, as plain facts. Each is a
// small read model with no presentation in it: a shell decides whether the
// document is a dial, a tab or a title, and a transport is keys or a menu.
// The operations beside them are actions in the ActionCatalog.

// One document a workspace has open, named by the key it takes back.
struct WorkspaceOpenDocument
{
    std::string Key;
    std::string Label;
    bool Changed = false;
};

// The document the workspace is editing right now, and, for a workspace that
// keeps several open, all of them.
struct WorkspaceDocumentFacet
{
    std::string Label;
    bool Changed = false;
    std::vector<WorkspaceOpenDocument> Open = {};
    // The key of the one being edited, among Open.
    std::string Active = {};
};

// Whether the workspace cooks and plays, and what that is doing now. The keys
// are the actions transport.cook, transport.play and transport.stop, which a
// workspace with a transport claims.
enum class TransportState : std::uint8_t
{
    None,
    Idle,
    Cooking,
    Playing,
    Failed,
};

struct WorkspaceTransportFacet
{
    TransportState State = TransportState::None;
    // A short report of what is happening: the cook step, the last failure.
    std::string Readout = {};
};

// What the workspace's hands are doing: the tool, or mode, in effect, what it is
// set to, and whether it has drafted work. Apply and Cancel are the actions
// tool.apply and tool.cancel, which a workspace with operations claims.
// An operation the workspace offers, and the action that puts it in hand.
struct OperationChoice
{
    std::string Label;
    IconId Icon = IconId::None;
    std::string Action;
    bool Current = false;
};

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

// How the workspace's hands act on what they hold: what a click selects, the
// gizmo's mode and axes, the grid, whether snapping is on. The settings
// themselves are actions (their categories: Mode, Transform, Grid), so a
// readout names them and a popover lists them.
struct WorkspaceManipulationFacet
{
    // Empty when the workspace manipulates nothing.
    std::string Element = {};
    std::string Mode = {};
    std::string Space = {};
    std::string Grid = {};
    bool Snap = false;
    bool Pivot = false; // the gizmo is moving the pivot, not the selection
};

// How the workspace is being seen: which view is in front and how many share
// the content area. The choices are actions in the View category.
struct WorkspaceViewFacet
{
    // Empty when the workspace has no views to arrange.
    std::string Projection = {};
    int Views = 0;
};

// A resource as shown: its name, and the host image that pictures it. Both are
// empty for an empty slot; the image alone is empty when nothing pictures it.
struct HeldResource
{
    std::string Label = {};
    std::string Image = {};
    // The action choosing it performs; empty when choosing it does nothing.
    std::string Action = {};
};

// What the workspace's operations consume, per role: the resource the next one
// will use, and those stashed beside it.
struct WorkspaceResourceFacet
{
    std::string Role = {};
    bool Primary = false;
    // The explorer that browses for another, or empty.
    std::string Explorer = {};
    HeldResource Active = {};
    std::vector<HeldResource> Stash = {};
};

// Properties the shell shows together, under a heading.
struct PropertySection
{
    std::string Heading = {};
    // Owned by the workspace, valid until its next frame.
    IPropertySource* Source = nullptr;
};

// One property of a source, named where something other than a sheet edits it.
struct PropertyRef
{
    IPropertySource* Source = nullptr;
    std::string Key = {};
};

// What is selected, as the shell presents it: what to call it, the actions
// that apply to it now, and its properties in sections. While an edit is
// staged (a previewed inset, bevel or bridge) the actions are how it applies
// or cancels.
struct WorkspaceSelectionFacet
{
    // Empty when nothing is selected.
    std::string Title = {};
    // Moves on whenever what is selected does, so anything held for the last
    // selection ends; 0 where selections are told apart by their title alone.
    std::uint64_t Revision = 0;
    // A line under it: how the selection is known, where it came from.
    std::string Subtitle = {};
    // The property the title is, when renaming it is an edit; empty when the
    // title only describes (three objects, five faces).
    PropertyRef TitleProperty = {};
    std::vector<std::string> Actions = {};
    std::vector<PropertySection> Sections = {};
    // Everything else about it, for when it is looked at closely: an object's
    // components, each a top-level group.
    std::vector<PropertySection> Details = {};
    bool Staged = false;
    // The properties are the work itself rather than a note about something in
    // the content: a data asset is its fields.
    bool Primary = false;
    // Looked at only when asked for -- the context wheel's More..., or the
    // palette -- rather than shown whenever something is selected; an edit
    // being staged still shows it, since that is how it applies.
    bool OnRequest = false;
};

// Something the workspace lets a person browse and pick from: its materials,
// its prefabs, the structure of its world. The workspace says what is in one
// and what picking does; the shell decides whether it is a tray, a sheet or a
// list, and pages through it so only what shows is ever asked for.
enum class ExplorerShape : std::uint8_t
{
    Pictures, // items are recognised by their picture: a grid of tiles
    Outline,  // items are recognised by their name and place in a tree
    Files,    // the project's content files of the kinds it accepts, chosen from the shelf
};

struct WorkspaceExplorerInfo
{
    std::string Id;
    std::string Title;
    ExplorerShape Shape = ExplorerShape::Pictures;
    // The action that opens it; the workspace defines it, the shell performs it.
    std::string Action;
    // For Files: the kinds it offers, the one in use, and any it must not
    // offer by asset path or path on disk (the level being edited, as a
    // prefab of itself).
    std::vector<ContentKind> Accepts = {};
    std::string Current = {};
    std::vector<std::string> Withheld = {};
};

struct ExplorerQuery
{
    std::string Explorer;
    // Items whose label holds this, or every item when it is empty.
    std::string Filter = {};
    std::size_t First = 0;
    std::size_t Count = 0;
};

// In an outline, whether an item holds others and whether they show.
enum class ExplorerBranch : std::uint8_t
{
    Leaf,
    Closed,
    Open,
};

struct ExplorerItem
{
    // What choosing it names back to the workspace.
    std::string Key;
    std::string Label;
    // A host image picturing it, or empty.
    std::string Image = {};
    int Depth = 0;
    ExplorerBranch Branch = ExplorerBranch::Leaf;
    // The one in hand now, or one of those selected.
    bool Current = false;
    // Small switches on the item, each showing its state by its icon: an eye
    // open or shut, a lock open or fastened. Choosing one names its index.
    std::vector<IconId> Marks = {};
};

// One page of an explorer's items that match a query.
struct ExplorerPage
{
    std::vector<ExplorerItem> Items;
    // How many match, on every page together.
    std::size_t Total = 0;
    // What to say when nothing matches.
    std::string Empty = {};
    // What the explorer offers besides its items: a rescan, a new item.
    std::vector<std::string> Actions = {};
};

// Counts one more item that matches and says whether it falls on the page the
// query asks for: every match counts toward the total, only the page's are kept.
[[nodiscard]] inline bool CountOnPage(ExplorerPage& page, const ExplorerQuery& query)
{
    const std::size_t index = page.Total++;
    return index >= query.First && page.Items.size() < query.Count;
}

// Choosing an item: picking it up alone, or putting it to use at once (a
// double click, Enter); in an outline, picking everything from the current one
// to it, picking it or letting it go beside the others, opening or closing it,
// or flipping one of its marks.
enum class ExplorerChoice : std::uint8_t
{
    Pick,
    Commit,
    Extend,
    Toggle,
    Open,
    Mark,
    // Shows the item -- its ancestors opened -- and chooses nothing.
    Reveal,
};

// One part of where the work is, and the item it reveals in the workspace's
// hierarchy explorer.
struct WorkspaceLocationPart
{
    std::string Label;
    std::string Key;
    // The part naming what is selected, rather than where.
    bool Selection = false;
};

// Where the work is, outermost first: for a world, the world, the graph and
// zone the camera is in, and what is selected. `Hierarchy` is the explorer
// the parts reveal items in, and the one Tab opens.
struct WorkspaceLocationFacet
{
    std::vector<WorkspaceLocationPart> Parts;
    std::string Hierarchy;
    // The action that shows the whole of where the work is from above; empty
    // where there is no such view.
    std::string MapAction = {};
};

// What a workspace whose work runs over time has on its clock, for a deck to
// show, scrub and play: the clock's name, the span it covers and where it is,
// in seconds, and what is marked along it. The transport is the timeline.*
// actions (play, restart, step_back, step_forward, loop, slower, faster,
// run_ahead, live) and the clock switch timeline.source.*, which a workspace
// with a timeline claims.
enum class TimelineMarkShape : std::uint8_t
{
    Round,
    Diamond,
};

struct TimelineMark
{
    std::string Key;
    double Time = 0.0;
    std::string Label = {};
    TimelineMarkShape Shape = TimelineMarkShape::Round;
    bool Selected = false;
    bool Movable = false;
};

// A stretch of the clock one thing held, such as the rule winning a layer
// from one decision to the next.
struct TimelineSpan
{
    std::string Key;
    double Start = 0.0;
    double End = 0.0;
    std::string Label = {};
    bool Selected = false;
};

// A value read at a moment on the clock, for a lane that plots.
struct TimelineSample
{
    double Time = 0.0;
    double Value = 0.0;
};

// A row under the ruler: what held along the clock, span by span, or a value
// plotted along it, sample by sample, scaled to the largest. A deck has room
// for kTimelineLaneLimit of them.
inline constexpr std::size_t kTimelineLaneLimit = 4;
struct TimelineLane
{
    std::string Label;
    std::vector<TimelineSpan> Spans = {};
    std::vector<TimelineSample> Samples = {};
};

struct WorkspaceTimelineFacet
{
    // Empty when the workspace keeps no time.
    std::string Clock = {};
    double Start = 0.0;
    double End = 0.0;
    double Time = 0.0;
    bool Playing = false;
    // Showing the running present rather than a moment scrubbed back to.
    bool Live = true;
    std::string Readout = {};
    std::vector<TimelineMark> Marks = {};
    std::vector<TimelineLane> Lanes = {};
};

// Where a drag along the deck is: pressed, moving, let go, or given up with
// nothing kept. A drag the workspace hears no end of (focus lost, Escape, its
// lens or clock switched) it cancels itself.
enum class TimelineDrag : std::uint8_t
{
    Begin,
    Move,
    End,
    Cancel,
};
