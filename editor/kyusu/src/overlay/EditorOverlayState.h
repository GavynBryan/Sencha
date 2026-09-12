#pragma once

#include "icons/IconId.h"
#include "ui/chrome/ChromeControls.h"
#include "selection/SelectableRef.h"
#include "viewport/ViewportId.h"

#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <optional>
#include <string>
#include <vector>

// A world-anchored text label drawn over the viewports (brush dimensions, etc).
// The viewport panel projects World to a pixel and draws Text there.
struct LabelRequest
{
    Vec3d World = {};
    Vec4 Color = {};
    std::string Text;
    // World axis (0/1/2) this label measures along, or -1 for a label that is not
    // axis-aligned. An ortho view hides the label for the axis it looks down (that
    // dimension is perpendicular to the screen, so its length can't be read).
    int Axis = -1;
};

// The line-from-start + distance readout shown while a manipulation drag is in
// flight. Drawn only in the viewport the drag started in, and cleared when no
// drag is active.
struct DragReadout
{
    std::optional<Vec3d> From;
    std::optional<Vec3d> To;
    std::string Text;
    ViewportId Viewport = {};

    void Clear()
    {
        From.reset();
        To.reset();
        Text.clear();
    }

    [[nodiscard]] bool Active() const { return From.has_value() && To.has_value(); }
};

// A world-space line drawn over the viewports: a tool's construction line, seen
// from every view where it lies rather than only the one it was drawn in.
// Viewport limits drawing when set.
struct WorldSegmentRequest
{
    Vec3d From = {};
    Vec3d To = {};
    Vec4 Color = {};
    float Thickness = 1.0f;
    ViewportId Viewport = {};
};

// A screen-sized square drawn at a world point. Viewport limits drawing when set.
struct PointHandleRequest
{
    Vec3d World = {};
    Vec4 Fill = {};
    Vec4 Border = {};
    ViewportId Viewport = {};
    float SizePixels = 8.0f;
};

// One button in a viewport button row. A button with no icon shows its label
// instead, which is how minus and plus get drawn without inventing artwork for
// two glyphs. One that is not enabled is not actionable either.
struct ViewportButton
{
    IconId Icon = IconId::None;
    std::string Label;
    bool Enabled = true;
    EditorChrome::ButtonTone Tone = EditorChrome::ButtonTone::Normal; // the panel buttons' own voice
};

// A readout the row places, not a control: drawn above the buttons it describes,
// and nothing hit-tests it.
struct ViewportButtonCaption
{
    std::string Text;
    int FirstButton = 0;
    int LastButton = 0;
};

// A row of buttons pinned to a world point, drawn over one viewport by the
// panel and hit-tested by the tool that asked for them.
//
// They are painted into the viewport's draw list rather than made into ImGui
// items, so there is no click ownership to negotiate with the viewport's own
// input: the tool tests the same rects in its own callbacks and consumes the
// press, exactly as it already does for its drag handles. Both sides place the
// row with ViewportButtons::Layout, so neither can drift from the other.
struct ViewportButtonRequest
{
    // The geometry the row is pinned to. It sits above the screen bounds of
    // these points, so it follows the shape rather than one corner of it.
    std::vector<Vec3d> Anchors;
    std::vector<ViewportButton> Buttons;
    std::optional<ViewportButtonCaption> Caption;
    int Hot = -1;           // the button the pointer is over, or -1
    ViewportId Viewport = {};
};

// A rotation dial lying in a face's plane, drawn over one viewport by the panel
// and hit-tested by the tool that asked for it.
//
// The same arrangement as the button row: painted into the viewport's draw list
// rather than made an ImGui item, so there is no click ownership to negotiate.
// This carries the dial's inputs rather than a finished radius, because the
// radius depends on the camera; both sides resolve it through
// ViewportDial::PlaceIn, which is what keeps the drawn circle and the
// hit-tested circle the same one.
struct ViewportDialRequest
{
    Vec3d Center = {};
    Vec3d AxisU = {};           // the angle runs from +U toward +V
    Vec3d AxisV = {};
    float BoxSemiMinor = 0.0f;  // the smaller half-extent of what the dial controls
    float Angle = 0.0f;         // where the knob sits
    float TickIncrement = 0.0f; // the stops a snapped turn lands on; 0 = free
    bool Hot = false;
    ViewportId Viewport = {};
};

// The element under the cursor for the active element mode. The renderer glows it
// to show it is selection-eligible; an edge also carries its length, drawn at
// MeasureAnchor. Element is invalid when nothing is hovered.
struct ElementHoverState
{
    SelectableRef Element;
    std::string Measure;     // e.g. edge length; empty when there is none
    Vec3d MeasureAnchor = {};
};

// Transient, render-only overlay inputs the viewport panel reads each frame.
// Producers (the per-frame selection-label rebuild, manipulation drags, the hover
// probe) write it. Never serialized, never undoable, so it cannot affect
// determinism or the document.
struct EditorOverlayState
{
    std::vector<LabelRequest> Labels;
    std::vector<WorldSegmentRequest> Segments;
    std::vector<PointHandleRequest> PointHandles;
    std::vector<ViewportButtonRequest> ViewportButtons;
    std::vector<ViewportDialRequest> ViewportDials;
    DragReadout Readout;
    ElementHoverState Hover;
    // A whole brush to highlight as a selected body while hovering, even when it is
    // not in the selection. The edge-cut tool sets it to the brush under the cursor
    // so the mesh the cut will affect (and then select) reads as selected. Invalid
    // when unused.
    EntityId HoverBody = {};
};
