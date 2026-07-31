#pragma once

#include "meshedit/MeshElementKind.h"

#include <cstdint>

// One Escape press climbs one level of editing context. The active-drag cancel
// lives ahead of this in the input chain (ViewportToolDispatcher), so by the
// time this policy runs there is no interaction in flight.
//
// These are context levels, not features, and the ordering between them is the
// policy: it is written out here so the whole of it can be read at once. Tools
// add no levels -- the dispatcher hands Escape to the active tool before this
// runs, and a tool cancels its own gesture there. Panel-driven previews add no
// levels either: the command stack holds a single pending-edit scope, so at most
// one such mechanism is ever staged, and the one CancelPendingEdit rung covers
// every one of them, present and future.
enum class EscapeAction : uint8_t
{
    CancelPendingEdit,     // drop a staged preview (bridge, inset, bevel)
    CancelGridOriginEdit,  // leave grid-origin editing, keep everything else
    CancelPivotEdit,       // leave pivot-editing, keep everything else
    ClearElementSelection, // drop vertex/edge/face refs, keep entity selection
    DropToObjectMode,      // element kind back to Object
    ClearSelection,        // deselect everything
    None,
};

// The editing context Escape reads, outermost level first.
struct EscapeContext
{
    // A panel-driven preview is staged (the cross-brush bridge, an inset or a
    // bevel). Tool-driven previews never reach here: the dispatcher hands
    // Escape to the active tool ahead of this policy.
    bool            HasPendingEdit = false;
    bool            GridOriginEditing = false;
    bool            PivotEditing = false;
    bool            HasElementRefs = false;
    MeshElementKind ElementKind = MeshElementKind::Object;
    bool            HasSelection = false;
};

[[nodiscard]] inline EscapeAction NextEscapeAction(const EscapeContext& context)
{
    if (context.HasPendingEdit)
        return EscapeAction::CancelPendingEdit;
    if (context.GridOriginEditing)
        return EscapeAction::CancelGridOriginEdit;
    if (context.PivotEditing)
        return EscapeAction::CancelPivotEdit;
    if (context.HasElementRefs)
        return EscapeAction::ClearElementSelection;
    if (context.ElementKind != MeshElementKind::Object)
        return EscapeAction::DropToObjectMode;
    if (context.HasSelection)
        return EscapeAction::ClearSelection;
    return EscapeAction::None;
}
