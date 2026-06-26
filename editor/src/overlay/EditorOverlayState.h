#pragma once

#include "../selection/SelectableRef.h"
#include "../viewport/ViewportId.h"

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
    DragReadout Readout;
    ElementHoverState Hover;
    // A whole brush to highlight as a selected body while hovering, even when it is
    // not in the selection. The edge-cut tool sets it to the brush under the cursor
    // so the mesh the cut will affect (and then select) reads as selected. Invalid
    // when unused.
    EntityId HoverBody = {};
};
