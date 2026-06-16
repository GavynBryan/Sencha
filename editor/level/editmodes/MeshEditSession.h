#pragma once

#include "../../editmodes/IEditSession.h"
#include "../../editmodes/IGizmo.h"

#include <memory>

struct ToolContext;
struct EditorViewport;

// The mesh-aware edit session. On pointer-down it computes the selection pivot
// for the active element mode, places the translate gizmo there, and — if an axis
// is hit — begins an axis-constrained drag. Object mode moves the entity transform
// (MoveEntityCommand); element modes move mesh vertices through MeshEditService.
// If no gizmo axis is hit it consumes nothing, letting the select tool pick.
//
// All state is read live from the ToolContext at pointer-down, so the session is
// correct after selection or mode (Shift+V) changes without being rebuilt.
// Replaces the whole-brush-only BrushEditSession. (Phase C.)
class MeshEditSession : public IEditSession
{
public:
    MeshEditSession();

    InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) override;

private:
    // The active gizmo. Today this is always a TranslateGizmo; when a gizmo mode
    // (move/rotate/scale) lands it selects which IGizmo lives here. The renderer
    // draws the matching gizmo type so its visual and clickable region agree.
    std::unique_ptr<IGizmo> Gizmo;
};
