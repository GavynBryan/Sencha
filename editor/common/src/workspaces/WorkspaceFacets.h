#pragma once

#include "icons/IconId.h"
#include "tools/ToolDraft.h"

#include <cstdint>
#include <string>
#include <vector>

class IPropertySource;

// What a workspace tells the shell about itself, as plain facts. Each is a
// small read model with no presentation in it: a shell decides whether the
// document is a dial, a tab or a title, and a transport is keys or a menu.
// The operations beside them are actions in the ActionCatalog.

// The document the workspace is editing right now.
struct WorkspaceDocumentFacet
{
    std::string Label;
    bool Changed = false;
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
};

// What the workspace's operations consume, per role: the resource the next one
// will use, and those stashed beside it. For the primary role, the actions in
// the Resource category use, store and clear the slots (resource.slot.N,
// resource.store.N, resource.clear.N) and browse for another (resource.browse).
struct WorkspaceResourceFacet
{
    std::string Role = {};
    bool Primary = false;
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

// What is selected, as the shell presents it: what to call it, the actions
// that apply to it now, and its properties in sections. While an edit is
// staged (a previewed inset, bevel or bridge) the actions are how it applies
// or cancels.
struct WorkspaceSelectionFacet
{
    // Empty when nothing is selected.
    std::string Title = {};
    std::vector<std::string> Actions = {};
    std::vector<PropertySection> Sections = {};
    // Everything else about it, for when it is looked at closely: an object's
    // components, each a top-level group.
    std::vector<PropertySection> Details = {};
    bool Staged = false;
};
