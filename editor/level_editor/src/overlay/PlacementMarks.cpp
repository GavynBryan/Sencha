#include "PlacementMarks.h"

#include "EditorOverlayState.h"

#include "EditorTheme.h"
#include "authoring/PlaceIcons.h"
#include "document/DocumentSerialization.h"
#include "document/placement/PlacementSession.h"

#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>

namespace
{
constexpr float kMarkWidth = 2.0f;

[[nodiscard]] IconId EntityMarker(const PlacementSubject& subject)
{
    if (subject.Key.empty())
        return IconId::Empty;
    const IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(subject.Key);
    return serializer != nullptr ? PlaceIconOf(*serializer) : IconId::None;
}
} // namespace

void AppendPlacementMarks(const PlacementSession& session, float gridSpacing, EditorOverlayState& overlay)
{
    overlay.Placement.Clear();
    const PendingPlacement* pending = session.Pending();
    if (pending == nullptr)
        return;

    const Vec4 color = pending->Refusal.empty() ? EditorTheme::PlacementMark : EditorTheme::ProblemError;
    const float reach = std::max(0.25f, gridSpacing * 0.5f);
    const Vec3d at = pending->Site.Point;
    const Vec3d facing = pending->Placement.Rotation.RotateVector(Vec3d::Forward());
    const Vec3d side = pending->Placement.Rotation.RotateVector(Vec3d::Right());
    const Vec3d up = pending->Site.Up;
    const Vec3d tip = at + facing * (reach * 2.0f);
    const Vec3d heel = tip - facing * (reach * 0.5f);

    std::vector<WorldSegmentRequest>& strokes = overlay.Placement.Strokes;
    const auto stroke = [&](Vec3d from, Vec3d to) {
        strokes.push_back({ .From = from, .To = to, .Color = color, .Thickness = kMarkWidth });
    };
    stroke(at - side * reach, at + side * reach);
    stroke(at - up * reach, at + up * reach);
    stroke(at, tip);
    stroke(tip, heel + side * (reach * 0.35f));
    stroke(tip, heel - side * (reach * 0.35f));

    if (pending->Subject.What == PlacementSubject::Kind::Entity)
        overlay.Markers.push_back({ .World = at, .Icon = EntityMarker(pending->Subject), .Selected = false });
}
