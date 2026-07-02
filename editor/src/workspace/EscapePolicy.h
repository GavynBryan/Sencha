#pragma once

#include "../meshedit/MeshElementKind.h"

#include <cstdint>

// One Escape press climbs one level of editing context. The active-drag cancel
// lives ahead of this in the input chain (ViewportToolDispatcher), so by the
// time this policy runs there is no interaction in flight.
enum class EscapeAction : uint8_t
{
    CancelGridOriginEdit,  // leave grid-origin editing, keep everything else
    CancelPivotEdit,       // leave pivot-editing, keep everything else
    ClearElementSelection, // drop vertex/edge/face refs, keep entity selection
    DropToObjectMode,      // element kind back to Object
    ClearSelection,        // deselect everything
    None,
};

[[nodiscard]] inline EscapeAction NextEscapeAction(bool gridOriginEditing,
                                                   bool pivotEditing,
                                                   bool hasElementRefs,
                                                   MeshElementKind elementKind,
                                                   bool hasSelection)
{
    if (gridOriginEditing)
        return EscapeAction::CancelGridOriginEdit;
    if (pivotEditing)
        return EscapeAction::CancelPivotEdit;
    if (hasElementRefs)
        return EscapeAction::ClearElementSelection;
    if (elementKind != MeshElementKind::Object)
        return EscapeAction::DropToObjectMode;
    if (hasSelection)
        return EscapeAction::ClearSelection;
    return EscapeAction::None;
}
