#pragma once

#include "../interaction/IInteraction.h"

#include <math/Quat.h>
#include <math/Vec.h>

#include <imgui.h>

#include <memory>
#include <vector>

struct EditorViewport;

// A transform delta expressed about the gizmo's pivot. Each gizmo fills only the
// channel it manipulates — translate fills Translation, a future rotate gizmo
// fills Rotation, a scale gizmo fills Scale — so handlers consume one uniform
// type regardless of which gizmo produced it. Identity in the unused channels.
struct GizmoDelta
{
    Vec3d Translation = {};
    Quatf Rotation = Quatf::Identity();
    Vec3d Scale = Vec3d(1.0f, 1.0f, 1.0f);
};

// Receives the delta produced by a gizmo drag (cumulative from drag start). The
// gizmo is agnostic about what moves; the handler interprets the delta — object
// transform, mesh vertices, anything.
struct IGizmoHandler
{
    virtual void Preview(const GizmoDelta& delta) = 0;
    virtual void Commit(const GizmoDelta& delta) = 0;
    virtual void Cancel() = 0;
    virtual ~IGizmoHandler() = default;
};

// One colored line segment of a gizmo's visual. Gizmos draw themselves as line
// lists so the renderer never hardcodes a gizmo's appearance (arrows vs rings vs
// scale handles): it just asks the active gizmo for its geometry.
struct GizmoLine
{
    Vec3d A = {};
    Vec3d B = {};
    Vec4 Color = {};
};

// A manipulation gizmo (translate now; rotate/scale later). Each concrete gizmo
// owns its own hit-testing, drag semantics, and visual. The active gizmo is
// chosen by the editor's gizmo mode; that mode is the "state", the gizmos are the
// classes. Parts are opaque non-zero ids the gizmo interprets (0 == miss).
struct IGizmo
{
    virtual void SetPivot(Vec3d pivot) = 0;
    virtual void ClearPivot() = 0;
    [[nodiscard]] virtual bool HasPivot() const = 0;

    [[nodiscard]] virtual int HitTest(const EditorViewport& viewport, ImVec2 screenPos) const = 0;
    [[nodiscard]] virtual std::unique_ptr<IInteraction> BeginDrag(
        int part,
        const EditorViewport& viewport,
        ImVec2 screenPos,
        std::unique_ptr<IGizmoHandler> handler) const = 0;

    virtual void AppendGeometry(const EditorViewport& viewport,
                                std::vector<GizmoLine>& out) const = 0;

    virtual ~IGizmo() = default;
};
