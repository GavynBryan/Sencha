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

} // namespace

void OfferManipulationActions(ActionCatalog& catalog, ActionContext& context, EditorWorkspace& workspace)
{
    EditorWorkspace* w = &workspace;
    const ActionOffers offer{ catalog, context };

    // What a click selects.
    for (const ElementModeRow& row : kElementModes)
        offer({ .Id = row.Id, .Label = row.Label, .Category = "Mode", .DefaultChords = { { row.Key, {} } } },
              { .Invoke = [w, kind = row.Kind] { w->MeshEdit.SetElementKind(kind); },
                .Checked = [w, kind = row.Kind] { return w->MeshEdit.GetElementKind() == kind; } });
    offer({ .Id = "mode.cycle", .Label = "Next Element Mode", .Category = "Mode",
            .DefaultChords = { { SDLK_V, { .Shift = true } } } },
          { .Invoke = [w] { (void)w->MeshEdit.CycleElementKind(); } });

    // The gizmo. Shift keeps its keys off the fly camera's bare letters; the
    // mode checked is the effective one, which is what the gizmo does now.
    for (const TransformModeItem& item : kTransformModeItems)
        offer({ .Id = item.Action, .Label = std::string(item.Choice.Label), .Category = "Transform",
                .Icon = item.Choice.Icon, .DefaultChords = { { item.Key, { .Shift = true } } } },
              { .Invoke = [w, mode = item.Mode] { w->Interaction.Manipulators->SetTransformMode(mode); },
                .Checked = [w, mode = item.Mode] { return w->Interaction.Manipulators->EffectiveMode() == mode; } });
    offer({ .Id = "gizmo.space", .Label = "Next Transform Space", .Category = "Transform",
            .DefaultChords = { { SDLK_G, { .Ctrl = true } } } },
          { .Invoke = [w] { w->Interaction.Manipulators->CycleTransformSpace(); } });
    for (const TransformSpaceItem& item : kTransformSpaceItems)
        offer({ .Id = item.Action, .Label = std::string(item.Choice.Label) + " Axes", .Category = "Transform",
                .Icon = item.Choice.Icon },
              { .Invoke = [w, space = item.Space] { w->Interaction.Manipulators->SetTransformSpace(space); },
                .Checked = [w, space = item.Space] { return w->Interaction.Manipulators->GetTransformSpace() == space; } });

    // The pivot, and moving a brush's origin to it or to its geometry.
    const auto hasSelection = [w] { return !w->Selection.GetSelection().empty(); };
    offer({ .Id = "pivot.edit", .Label = "Edit Pivot", .Category = "Transform", .Icon = IconId::Pivot },
          { .Invoke = [w] { w->Interaction.Manipulators->SetEditingPivot(!w->Interaction.Manipulators->IsEditingPivot()); },
            .Enabled = hasSelection,
            .Checked = [w] { return w->Interaction.Manipulators->IsEditingPivot(); } });
    offer({ .Id = "origin.pivot", .Label = "Origin to Pivot", .Category = "Transform", .Icon = IconId::Anchor },
          { .Invoke = [w] { w->SetSelectedBrushOriginToPivot(); },
            .Enabled = [w] { return w->Interaction.Manipulators->HasPivotOverride(); } });
    offer({ .Id = "origin.vertex", .Label = "Origin to Selected Vertex", .Category = "Transform",
            .Icon = IconId::Anchor },
          { .Invoke = [w] { w->SetSelectedBrushOrigin(EditorWorkspace::OriginAnchor::SelectedVertex); },
            .Enabled = hasSelection });
    offer({ .Id = "origin.center", .Label = "Origin to Bounds Center", .Category = "Transform",
            .Icon = IconId::Anchor },
          { .Invoke = [w] { w->SetSelectedBrushOrigin(EditorWorkspace::OriginAnchor::BoundsCenter); },
            .Enabled = hasSelection });
    offer({ .Id = "origin.corner", .Label = "Origin to Bounds Corner", .Category = "Transform",
            .Icon = IconId::Anchor },
          { .Invoke = [w] { w->SetSelectedBrushOrigin(EditorWorkspace::OriginAnchor::BoundsMinCorner); },
            .Enabled = hasSelection });

    // Snapping and the grid.
    offer({ .Id = "snap.toggle", .Label = "Snap", .Category = "Grid", .Icon = IconId::Snap },
          { .Invoke = [w] { w->Grid.SnapEnabled = !w->Grid.SnapEnabled; },
            .Checked = [w] { return w->Grid.SnapEnabled; } });
    for (const SnapRow& row : kSnapTargets)
        offer({ .Id = row.Id, .Label = row.Label, .Category = "Grid" },
              { .Invoke = [w, target = row.Target] { w->Grid.Target = target; },
                .Checked = [w, target = row.Target] { return w->Grid.Target == target; } });
    offer({ .Id = "grid.finer", .Label = "Finer Grid", .Category = "Grid",
            .DefaultChords = { { SDLK_LEFTBRACKET, {} } } },
          { .Invoke = [w] { w->Grid.StepSpacing(-1); } });
    offer({ .Id = "grid.coarser", .Label = "Coarser Grid", .Category = "Grid",
            .DefaultChords = { { SDLK_RIGHTBRACKET, {} } } },
          { .Invoke = [w] { w->Grid.StepSpacing(+1); } });
    for (const float size : GridSettings::kSpacingSteps)
        offer({ .Id = std::format("grid.size.{}", size), .Label = std::format("Grid {}", size), .Category = "Grid",
                .Icon = IconId::Grid },
              { .Invoke = [w, size] { w->Grid.Spacing = size; }, .Checked = [w, size] { return w->Grid.Spacing == size; } });
    offer({ .Id = "grid.move_origin", .Label = "Move Grid Origin", .Category = "Grid", .Icon = IconId::GridFrame },
          { .Invoke = [w] { w->Interaction.Manipulators->SetEditingGridOrigin(!w->Interaction.Manipulators->IsEditingGridOrigin()); },
            .Checked = [w] { return w->Interaction.Manipulators->IsEditingGridOrigin(); } });
    offer({ .Id = "grid.origin_selection", .Label = "Grid Origin to Selection", .Category = "Grid",
            .Icon = IconId::GridFrame, .DefaultChords = { { SDLK_G, { .Shift = true } } } },
          { .Invoke = [w] { w->SetGridOriginToSelection(); } });
    offer({ .Id = "grid.align_face", .Label = "Align Grid to Face", .Category = "Grid", .Icon = IconId::GridFrame,
            .DefaultChords = { { SDLK_G, { .Alt = true } } } },
          { .Invoke = [w] { w->AlignGridToSelectedFace(); } });
    offer({ .Id = "grid.rotate", .Label = "Rotate Grid 90\xC2\xB0", .Category = "Grid", .Icon = IconId::GridFrame },
          { .Invoke = [w] { w->RotateGridInPlane(90.0f); } });
    offer({ .Id = "grid.reset", .Label = "Reset Grid", .Category = "Grid", .Icon = IconId::GridFrame,
            .DefaultChords = { { SDLK_G, { .Ctrl = true, .Shift = true } } } },
          { .Invoke = [w] { w->ResetGrid(); }, .Enabled = [w] { return w->Grid.HasCustomFrame(); } });

    OfferAction(catalog, context, FrameAction(),
                { .Invoke = [w] { (void)w->FrameSelection(); },
                  .Enabled = [w] { return !w->Selection.GetSelection().empty(); } });
    // What the views show beyond the scene.
    offer({ .Id = "view.zone_bounds", .Label = "Zone Bounds", .Category = "View", .Icon = IconId::ZoneBounds },
          { .Invoke = [w] { w->WorldView.ShowZoneBounds = !w->WorldView.ShowZoneBounds; },
            .Checked = [w] { return w->WorldView.ShowZoneBounds; } });
    offer({ .Id = "view.zone_graph", .Label = "Zone Graph", .Category = "View" },
          { .Invoke = [w] { w->WorldView.ShowZoneGraph = !w->WorldView.ShowZoneGraph; },
            .Enabled = [w] { return w->World.IsWorld(); },
            .Checked = [w] { return w->WorldView.ShowZoneGraph; } });
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    for (std::uint32_t index = 0; index < kRenderDebugViewCount; ++index)
    {
        const RenderDebugView view = static_cast<RenderDebugView>(index);
        offer({ .Id = std::format("view.debug.{}", index),
                .Label = std::format("Show: {}", RenderDebugViewLabel(view)), .Category = "View" },
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
