#include "workspace/ManipulationActions.h"

#include "actions/ActionOffer.h"
#include "editmodes/ManipulatorSession.h"
#include "editmodes/TransformModeItems.h"
#include "editmodes/TransformSpace.h"
#include "meshedit/MeshElementKindTraits.h"
#include "workspace/EditorWorkspace.h"

#include <render/RenderDebugView.h>

#include <SDL3/SDL_keycode.h>

#include <array>
#include <format>

namespace
{
struct ElementModeRow
{
    MeshElementKind Kind;
    const char* Id;
    const char* Label;
    SDL_Keycode Key;
};
constexpr std::array<ElementModeRow, 4> kElementModes = { {
    { MeshElementKind::Object, "mode.object", "Object Mode", SDLK_1 },
    { MeshElementKind::Vertex, "mode.vertex", "Vertex Mode", SDLK_2 },
    { MeshElementKind::Edge, "mode.edge", "Edge Mode", SDLK_3 },
    { MeshElementKind::Face, "mode.face", "Face Mode", SDLK_4 },
} };

struct SpaceRow
{
    TransformSpace Space;
    const char* Id;
    const char* Label;
};
constexpr std::array<SpaceRow, 3> kSpaces = { {
    { TransformSpace::Grid, "gizmo.space.grid", "Grid Axes" },
    { TransformSpace::World, "gizmo.space.world", "World Axes" },
    { TransformSpace::Local, "gizmo.space.local", "Local Axes" },
} };

struct SnapRow
{
    SnapTarget Target;
    const char* Id;
    const char* Label;
};
constexpr std::array<SnapRow, 4> kSnapTargets = { {
    { SnapTarget::Grid, "snap.grid", "Snap to Grid Lines" },
    { SnapTarget::Vertex, "snap.vertex", "Snap to Vertices" },
    { SnapTarget::Edge, "snap.edge", "Snap to Edges" },
    { SnapTarget::Face, "snap.face", "Snap to Faces" },
} };

const char* TransformActionId(TransformMode mode)
{
    switch (mode)
    {
    case TransformMode::Resize: return "gizmo.resize";
    case TransformMode::Move:   return "gizmo.move";
    case TransformMode::Rotate: return "gizmo.rotate";
    case TransformMode::Scale:
    default:                    return "gizmo.scale";
    }
}

SDL_Keycode TransformKey(TransformMode mode)
{
    switch (mode)
    {
    case TransformMode::Resize: return SDLK_Q;
    case TransformMode::Move:   return SDLK_W;
    case TransformMode::Rotate: return SDLK_E;
    case TransformMode::Scale:
    default:                    return SDLK_R;
    }
}
} // namespace

void OfferManipulationActions(ActionCatalog& catalog, ActionContext& context, EditorWorkspace& workspace)
{
    EditorWorkspace* w = &workspace;
    const auto offer = [&](std::string id, std::string label, std::string category, std::vector<KeyChord> chords,
                           IconId icon, ActionHandler handler) {
        OfferAction(catalog, context,
                    { .Id = std::move(id), .Label = std::move(label), .Category = std::move(category), .Icon = icon,
                      .DefaultChords = std::move(chords) },
                    std::move(handler));
    };

    // What a click selects.
    for (const ElementModeRow& row : kElementModes)
        offer(row.Id, row.Label, "Mode", { { row.Key, {} } }, IconId::None,
              { .Invoke = [w, kind = row.Kind] { w->MeshEdit.SetElementKind(kind); },
                .Checked = [w, kind = row.Kind] { return w->MeshEdit.GetElementKind() == kind; } });
    offer("mode.cycle", "Next Element Mode", "Mode", { { SDLK_V, { .Shift = true } } }, IconId::None,
          { .Invoke = [w] { (void)w->MeshEdit.CycleElementKind(); } });

    // The gizmo. Shift keeps its keys off the fly camera's bare letters; the
    // mode checked is the effective one, which is what the gizmo does now.
    for (const TransformModeItem& item : kTransformModeItems)
        offer(TransformActionId(item.Mode), std::string(item.Choice.Label), "Transform",
              { { TransformKey(item.Mode), { .Shift = true } } }, item.Choice.Icon,
              { .Invoke = [w, mode = item.Mode] { w->Interaction.Manipulators->SetTransformMode(mode); },
                .Checked = [w, mode = item.Mode] { return w->Interaction.Manipulators->EffectiveMode() == mode; } });
    offer("gizmo.space", "Next Transform Space", "Transform", { { SDLK_G, { .Ctrl = true } } }, IconId::None,
          { .Invoke = [w] { w->Interaction.Manipulators->CycleTransformSpace(); } });
    for (const SpaceRow& row : kSpaces)
        offer(row.Id, row.Label, "Transform", {}, IconId::None,
              { .Invoke = [w, space = row.Space] { w->Interaction.Manipulators->SetTransformSpace(space); },
                .Checked = [w, space = row.Space] { return w->Interaction.Manipulators->GetTransformSpace() == space; } });

    // The pivot, and moving a brush's origin to it or to its geometry.
    const auto hasSelection = [w] { return !w->Selection.GetSelection().empty(); };
    offer("pivot.edit", "Edit Pivot", "Transform", {}, IconId::Pivot,
          { .Invoke = [w] { w->Interaction.Manipulators->SetEditingPivot(!w->Interaction.Manipulators->IsEditingPivot()); },
            .Enabled = hasSelection,
            .Checked = [w] { return w->Interaction.Manipulators->IsEditingPivot(); } });
    offer("origin.pivot", "Origin to Pivot", "Transform", {}, IconId::Anchor,
          { .Invoke = [w] { w->SetSelectedBrushOriginToPivot(); },
            .Enabled = [w] { return w->Interaction.Manipulators->HasPivotOverride(); } });
    offer("origin.vertex", "Origin to Selected Vertex", "Transform", {}, IconId::Anchor,
          { .Invoke = [w] { w->SetSelectedBrushOrigin(EditorWorkspace::OriginAnchor::SelectedVertex); },
            .Enabled = hasSelection });
    offer("origin.center", "Origin to Bounds Center", "Transform", {}, IconId::Anchor,
          { .Invoke = [w] { w->SetSelectedBrushOrigin(EditorWorkspace::OriginAnchor::BoundsCenter); },
            .Enabled = hasSelection });
    offer("origin.corner", "Origin to Bounds Corner", "Transform", {}, IconId::Anchor,
          { .Invoke = [w] { w->SetSelectedBrushOrigin(EditorWorkspace::OriginAnchor::BoundsMinCorner); },
            .Enabled = hasSelection });

    // Snapping and the grid.
    offer("snap.toggle", "Snap", "Grid", {}, IconId::Snap,
          { .Invoke = [w] { w->Grid.SnapEnabled = !w->Grid.SnapEnabled; },
            .Checked = [w] { return w->Grid.SnapEnabled; } });
    for (const SnapRow& row : kSnapTargets)
        offer(row.Id, row.Label, "Grid", {}, IconId::None,
              { .Invoke = [w, target = row.Target] { w->Grid.Target = target; },
                .Checked = [w, target = row.Target] { return w->Grid.Target == target; } });
    offer("grid.finer", "Finer Grid", "Grid", { { SDLK_LEFTBRACKET, {} } }, IconId::None,
          { .Invoke = [w] { w->Grid.StepSpacing(-1); } });
    offer("grid.coarser", "Coarser Grid", "Grid", { { SDLK_RIGHTBRACKET, {} } }, IconId::None,
          { .Invoke = [w] { w->Grid.StepSpacing(+1); } });
    for (const float size : GridSettings::kSpacingSteps)
        offer(std::format("grid.size.{}", size), std::format("Grid {}", size), "Grid", {}, IconId::Grid,
              { .Invoke = [w, size] { w->Grid.Spacing = size; }, .Checked = [w, size] { return w->Grid.Spacing == size; } });
    offer("grid.move_origin", "Move Grid Origin", "Grid", {}, IconId::GridFrame,
          { .Invoke = [w] { w->Interaction.Manipulators->SetEditingGridOrigin(!w->Interaction.Manipulators->IsEditingGridOrigin()); },
            .Checked = [w] { return w->Interaction.Manipulators->IsEditingGridOrigin(); } });
    offer("grid.origin_selection", "Grid Origin to Selection", "Grid", { { SDLK_G, { .Shift = true } } },
          IconId::GridFrame, { .Invoke = [w] { w->SetGridOriginToSelection(); } });
    offer("grid.align_face", "Align Grid to Face", "Grid", { { SDLK_G, { .Alt = true } } }, IconId::GridFrame,
          { .Invoke = [w] { w->AlignGridToSelectedFace(); } });
    offer("grid.rotate", "Rotate Grid 90\xC2\xB0", "Grid", {}, IconId::GridFrame,
          { .Invoke = [w] { w->RotateGridInPlane(90.0f); } });
    offer("grid.reset", "Reset Grid", "Grid", { { SDLK_G, { .Ctrl = true, .Shift = true } } }, IconId::GridFrame,
          { .Invoke = [w] { w->ResetGrid(); }, .Enabled = [w] { return w->Grid.HasCustomFrame(); } });

    // What the views show beyond the scene.
    offer("view.zone_bounds", "Zone Bounds", "View", {}, IconId::ZoneBounds,
          { .Invoke = [w] { w->WorldView.ShowZoneBounds = !w->WorldView.ShowZoneBounds; },
            .Checked = [w] { return w->WorldView.ShowZoneBounds; } });
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    for (std::uint32_t index = 0; index < kRenderDebugViewCount; ++index)
    {
        const RenderDebugView view = static_cast<RenderDebugView>(index);
        offer(std::format("view.debug.{}", index), std::format("Show: {}", RenderDebugViewLabel(view)), "View", {},
              IconId::None,
              { .Invoke = [w, view] { w->WorldView.DebugViewMode = view; },
                .Checked = [w, view] { return w->WorldView.DebugViewMode == view; } });
    }
#endif
}

WorkspaceManipulationFacet ManipulationOf(const EditorWorkspace& workspace)
{
    const ManipulatorSession* session = workspace.Interaction.Manipulators;
    WorkspaceManipulationFacet facet;
    facet.Element = Traits(workspace.MeshEdit.GetElementKind()).Label;
    if (session != nullptr)
    {
        for (const TransformModeItem& item : kTransformModeItems)
            if (item.Mode == session->EffectiveMode())
                facet.Mode = std::string(item.Choice.Label);
        facet.Space = TransformSpaceLabel(session->GetTransformSpace());
        facet.Pivot = session->IsEditingPivot();
    }
    facet.Grid = std::format("{}", workspace.Grid.Spacing);
    facet.Snap = workspace.Grid.SnapEnabled;
    return facet;
}
