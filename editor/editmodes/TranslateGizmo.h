#pragma once

#include "IGizmo.h"

#include <math/Vec.h>

// Reusable 3-axis translate gizmo. Hit-tests the X/Y/Z axes in screen space and
// produces an axis-constrained, grid-snapped drag whose GizmoDelta carries only a
// translation. Draws itself as 3 colored arrows. Knows nothing about brushes.
// (Phase C mesh-edit gizmo; first concrete IGizmo.)
class TranslateGizmo : public IGizmo
{
public:
    void SetPivot(Vec3d pivot) override;
    void ClearPivot() override;
    [[nodiscard]] bool HasPivot() const override;
    [[nodiscard]] Vec3d GetPivot() const;

    [[nodiscard]] int HitTest(const EditorViewport& viewport, ImVec2 screenPos) const override;
    [[nodiscard]] std::unique_ptr<IInteraction> BeginDrag(
        int part,
        const EditorViewport& viewport,
        ImVec2 screenPos,
        std::unique_ptr<IGizmoHandler> handler) const override;
    void AppendGeometry(const EditorViewport& viewport,
                        std::vector<GizmoLine>& out) const override;

    [[nodiscard]] static Vec3d AxisDirection(int axis);
    [[nodiscard]] static float AxisLength(const EditorViewport& viewport, Vec3d pivot);

private:
    Vec3d Pivot = {};
    bool HasPivot_ = false;
};
