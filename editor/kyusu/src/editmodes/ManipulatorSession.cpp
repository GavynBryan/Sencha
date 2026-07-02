#include "ManipulatorSession.h"

#include "BoundsManipulator.h"
#include "RotateManipulator.h"
#include "ScaleManipulator.h"
#include "TranslateManipulator.h"
#include "interaction/InteractionHost.h"
#include "meshedit/ManipulationSink.h"
#include "meshedit/MeshEditService.h"
#include "selection/SelectionService.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"

#include <imgui.h>

#include <utility>

ManipulatorSession::ManipulatorSession(SelectionService& selection,
                                       MeshEditService& service,
                                       ManipulationSink& sink,
                                       GridSettings& grid,
                                       PivotState& pivot)
    : Selection(selection)
    , Service(service)
    , Sink(sink)
    , Grid(grid)
    , Pivot(pivot)
{
    // The only registration site: new manipulators land here and nowhere else. The
    // active TransformMode filters to one gizmo, so order only matters among same
    // mode manipulators (none today).
    Manipulators.push_back(std::make_unique<BoundsManipulator>());
    Manipulators.push_back(std::make_unique<TranslateManipulator>());
    Manipulators.push_back(std::make_unique<RotateManipulator>());
    Manipulators.push_back(std::make_unique<ScaleManipulator>());
}

void ManipulatorSession::SetTransformMode(TransformMode mode)
{
    ActiveMode = mode;
    Memory.Record(Service.GetElementKind() != MeshElementKind::Object, mode);
}

void ManipulatorSession::OnElementKindChanged(MeshElementKind next)
{
    ActiveMode = Memory.ModeFor(next != MeshElementKind::Object);
}

std::array<Vec3d, 3> ManipulatorSession::GizmoAxes() const
{
    constexpr std::array<Vec3d, 3> kWorld = { Vec3d{ 1, 0, 0 }, Vec3d{ 0, 1, 0 }, Vec3d{ 0, 0, 1 } };
    switch (Space)
    {
    case TransformSpace::World:
        return kWorld;

    case TransformSpace::Grid:
    {
        // (U, V x U, V) is right-handed for any orthonormal U, V; for the default
        // frame it is exactly the world axes.
        const Vec3d normal = Grid.AxisV.Cross(Grid.AxisU);
        if (normal.SqrMagnitude() < 1e-12f)
            return kWorld;
        return { Grid.AxisU, normal.Normalized(), Grid.AxisV };
    }

    case TransformSpace::Local:
    {
        const SelectableRef primary = Selection.GetPrimarySelection();
        if (!primary.Entity.IsValid())
            return kWorld;
        const std::optional<Transform3f> transform = Sink.ResolveTransform(primary.Entity);
        if (!transform.has_value())
            return kWorld;
        return {
            transform->Rotation.RotateVector(Vec3d{ 1, 0, 0 }),
            transform->Rotation.RotateVector(Vec3d{ 0, 1, 0 }),
            transform->Rotation.RotateVector(Vec3d{ 0, 0, 1 }),
        };
    }
    }
    return kWorld;
}

ManipulatorContext ManipulatorSession::MakeContext(const SelectionSnapshot& snapshot) const
{
    return ManipulatorContext{
        .Selection = snapshot,
        .Service = Service,
        .Sink = Sink,
        .Grid = Grid,
        .Pivot = Pivot,
        .Axes = GizmoAxes(),
        .EditGridOrigin = GridOriginEditing,
    };
}

TransformMode ManipulatorSession::EffectiveMode() const
{
    // Grid editing drives the frame with Move (origin) or Rotate (axes); the
    // other gizmos have no grid meaning and yield to Move.
    if (GridOriginEditing
        && ActiveMode != TransformMode::Move && ActiveMode != TransformMode::Rotate)
        return TransformMode::Move;
    if (ActiveMode != TransformMode::Resize)
        return ActiveMode;
    if (Service.GetElementKind() != MeshElementKind::Object)
        return TransformMode::Move;
    if (ResizableQuery && !ResizableQuery())
        return TransformMode::Move;
    return ActiveMode;
}

InputConsumed ManipulatorSession::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const ImVec2 pos = pointer.Position;
    const SelectionSnapshot snapshot = Selection.GetSnapshot();
    const ManipulatorContext mctx = MakeContext(snapshot);
    const TransformMode mode = EffectiveMode();

    for (const std::unique_ptr<IManipulator>& manipulator : Manipulators)
    {
        if (manipulator->Mode() != mode)
            continue;
        if (!manipulator->AppliesTo(mctx, viewport))
            continue;
        const int part = manipulator->HitTest(mctx, viewport, pos);
        if (part == 0)
            continue;
        if (auto interaction = manipulator->BeginDrag(part, mctx, viewport, pos, pointer.Modifiers))
        {
            ctx.Interactions.Begin(ctx, std::move(interaction));
            return InputConsumed::Yes;
        }
    }

    return InputConsumed::No;
}

void ManipulatorSession::BuildVisuals(const EditorViewport& viewport, ManipulatorVisual& out) const
{
    const SelectionSnapshot snapshot = Selection.GetSnapshot();
    const ManipulatorContext mctx = MakeContext(snapshot);
    const TransformMode mode = EffectiveMode();

    // Hover: when the cursor is over this viewport, find the part it would grab —
    // in the same priority order routing uses, so only the manipulator that would
    // receive the click shows a hovered part.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool inViewport = mouse.x >= viewport.RegionMin.x && mouse.x <= viewport.RegionMax.x
                         && mouse.y >= viewport.RegionMin.y && mouse.y <= viewport.RegionMax.y;
    int hoveredIndex = -1;
    int hoveredPart = 0;
    if (inViewport)
    {
        for (std::size_t i = 0; i < Manipulators.size(); ++i)
        {
            if (Manipulators[i]->Mode() != mode)
                continue;
            if (!Manipulators[i]->AppliesTo(mctx, viewport))
                continue;
            if (const int part = Manipulators[i]->HitTest(mctx, viewport, mouse))
            {
                hoveredIndex = static_cast<int>(i);
                hoveredPart = part;
                break;
            }
        }
    }

    for (std::size_t i = 0; i < Manipulators.size(); ++i)
    {
        if (Manipulators[i]->Mode() != mode)
            continue;
        if (!Manipulators[i]->AppliesTo(mctx, viewport))
            continue;
        Manipulators[i]->BuildVisual(mctx, viewport,
                                     static_cast<int>(i) == hoveredIndex ? hoveredPart : 0, out);
    }
}
