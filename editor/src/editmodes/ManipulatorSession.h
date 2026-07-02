#pragma once

#include "IManipulator.h"
#include "TransformModeMemory.h"
#include "TransformSpace.h"

#include "../input/InputEvent.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

struct ToolContext;
struct EditorViewport;
class SelectionService;
class MeshEditService;
struct ManipulationSink;
struct GridSettings;

// Generic edit session: owns the ordered manipulator list and routes a
// pointer-down to the first applicable manipulator that gets a hit, beginning its
// drag. Holds no scene knowledge — it reads selection/mode and previews/commits
// only through the injected SelectionService, MeshEditService, and
// ManipulationSink. Replaces the brush-coupled MeshEditSession. Adding a
// manipulator is a push_back here; nothing else changes. (08-select-tool-v2.md)
class ManipulatorSession
{
public:
    ManipulatorSession(SelectionService& selection, MeshEditService& service, ManipulationSink& sink,
                       GridSettings& grid, PivotState& pivot);

    InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer);

    // Visuals for every applicable manipulator (drawn by the overlay renderer).
    void BuildVisuals(const EditorViewport& viewport, ManipulatorVisual& out) const;

    // The active transform gizmo. Only manipulators whose Mode() matches it are
    // drawn and hit-tested, so switching modes swaps the visible gizmo.
    [[nodiscard]] TransformMode GetTransformMode() const { return ActiveMode; }
    void SetTransformMode(TransformMode mode);

    // The mode actually drawn/routed. Resize only applies to whole brushes, so in a
    // vertex/edge/face mode, or when the selection has nothing bounds-resizable, it
    // falls back to Move (there is always a working gizmo) without discarding the
    // user's chosen mode. UI highlights this, not ActiveMode.
    [[nodiscard]] TransformMode EffectiveMode() const;

    // Called when the element kind changes: restores the gizmo last used in the
    // entered context (object vs element), so leaving Face mode does not carry the
    // element gizmo onto whole-object work and vice versa.
    void OnElementKindChanged(MeshElementKind next);

    // True when the current selection can be bounds-resized (it resolves to at
    // least one brush). Set by the workspace, which owns scene access; unset, the
    // Resize fallback only considers the element kind.
    void SetResizableQuery(std::function<bool()> query) { ResizableQuery = std::move(query); }

    // True once the pivot has been moved off the computed center (so "set origin to
    // pivot" has something to do).
    [[nodiscard]] bool HasPivotOverride() const { return Pivot.Override.has_value(); }

    // Pivot edit: the Move gizmo moves the transient pivot instead of the selection,
    // so enabling it switches to the Move gizmo (the only one that drives the pivot).
    [[nodiscard]] bool IsEditingPivot() const { return Pivot.Editing; }
    void SetEditingPivot(bool editing)
    {
        Pivot.Editing = editing;
        if (editing)
        {
            GridOriginEditing = false; // the Move gizmo has one target at a time
            ActiveMode = TransformMode::Move;
        }
    }

    // Grid-origin edit: the Move gizmo drags the workspace grid origin (the grid
    // analog of pivot editing). Works with or without a selection.
    [[nodiscard]] bool IsEditingGridOrigin() const { return GridOriginEditing; }
    void SetEditingGridOrigin(bool editing)
    {
        GridOriginEditing = editing;
        if (editing)
        {
            Pivot.Editing = false;
            ActiveMode = TransformMode::Move;
        }
    }

    // The frame the gizmos draw and drag along (see TransformSpace).
    [[nodiscard]] TransformSpace GetTransformSpace() const { return Space; }
    void SetTransformSpace(TransformSpace space) { Space = space; }
    TransformSpace CycleTransformSpace()
    {
        Space = NextTransformSpace(Space);
        return Space;
    }

private:
    // The gizmo axes for the active TransformSpace (world axes for World, the
    // grid frame for Grid, the primary entity's rotated axes for Local; both
    // fall back to world when they cannot resolve).
    [[nodiscard]] std::array<Vec3d, 3> GizmoAxes() const;
    [[nodiscard]] ManipulatorContext MakeContext(const SelectionSnapshot& snapshot) const;

    SelectionService& Selection;
    MeshEditService& Service;
    ManipulationSink& Sink;
    GridSettings& Grid;
    PivotState& Pivot;
    std::vector<std::unique_ptr<IManipulator>> Manipulators;
    std::function<bool()> ResizableQuery;
    TransformModeMemory Memory;
    TransformSpace Space = TransformSpace::Grid;
    bool GridOriginEditing = false;
    // Selecting a brush shows the resize-bounds gizmo by default (it now works in
    // perspective too); Move/Rotate/Scale are a key or button away.
    TransformMode ActiveMode = TransformMode::Resize;
};
