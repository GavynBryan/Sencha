#pragma once

#include "IManipulator.h"

#include "../input/InputEvent.h"

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
                       const GridSettings& grid, PivotState& pivot);

    InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer);

    // Visuals for every applicable manipulator (drawn by the overlay renderer).
    void BuildVisuals(const EditorViewport& viewport, ManipulatorVisual& out) const;

    // The active transform gizmo. Only manipulators whose Mode() matches it are
    // drawn and hit-tested, so switching modes swaps the visible gizmo.
    [[nodiscard]] TransformMode GetTransformMode() const { return ActiveMode; }
    void SetTransformMode(TransformMode mode) { ActiveMode = mode; }

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
            ActiveMode = TransformMode::Move;
    }

private:
    // The mode actually drawn/routed. Resize only applies to whole objects, so in a
    // vertex/edge/face mode it falls back to Move (so element editing always has a
    // gizmo) without changing the user's chosen mode.
    [[nodiscard]] TransformMode EffectiveMode() const;

    SelectionService& Selection;
    MeshEditService& Service;
    ManipulationSink& Sink;
    const GridSettings& Grid;
    PivotState& Pivot;
    std::vector<std::unique_ptr<IManipulator>> Manipulators;
    // Selecting a brush shows the resize-bounds gizmo by default (it now works in
    // perspective too); Move/Rotate/Scale are a key or button away.
    TransformMode ActiveMode = TransformMode::Resize;
};
