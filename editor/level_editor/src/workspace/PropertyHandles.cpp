#include "workspace/PropertyHandles.h"

#include "EditorTheme.h"
#include "authoring/ComponentPropertySource.h"
#include "ui/EditorUiStyle.h"
#include "viewport/ViewportProjection.h"
#include "workspace/EditorWorkspace.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

namespace
{
constexpr PropertyHandleRule kRules[] = {
    { "PointLight", "range", PropertyHandleKind::Radius },
    { "PointLight", "intensity", PropertyHandleKind::Scroll },
    { "SpotLight", "range", PropertyHandleKind::Radius },
    { "SpotLight", "intensity", PropertyHandleKind::Scroll },
};

// Frames without a wheel turn after which a scroll is one step.
constexpr int kRestFrames = 20;
constexpr int kRingSegments = 64;
// How near the pointer must be, in design pixels, to take a knob or to scroll
// over the object.
constexpr float kKnobReachDp = 12.0f;
constexpr float kObjectReachDp = 24.0f;

std::optional<PropertyHandleKind> KindOf(std::string_view key)
{
    for (const PropertyHandleRule& rule : kRules)
        if (key.size() == rule.Component.size() + 1 + rule.Field.size() && key.starts_with(rule.Component)
            && key[rule.Component.size()] == '/' && key.ends_with(rule.Field))
            return rule.Kind;
    return std::nullopt;
}

// Screen right and up as world directions, for a ring that faces the view.
std::pair<Vec3d, Vec3d> Facing(const EditorViewport& viewport)
{
    const Vec3d forward = viewport.Camera.GetForwardVector().Normalized();
    Vec3d right = forward.Cross(Vec3d(0.0f, 1.0f, 0.0f));
    if (right.SqrMagnitude() < 1e-6f)
        right = Vec3d(1.0f, 0.0f, 0.0f);
    right = right.Normalized();
    return { right, right.Cross(forward).Normalized() };
}

bool Shown(const EditorViewport& viewport)
{
    return viewport.RegionMax.X > viewport.RegionMin.X && viewport.RegionMax.Y > viewport.RegionMin.Y;
}

std::string Text(float value)
{
    return std::format("{:.2f}", value);
}
} // namespace

PropertyHandles::PropertyHandles(EditorWorkspace& workspace, ComponentPropertySource& components)
    : Workspace(workspace)
    , Components(components)
{
}

std::vector<PropertyHandles::Handle> PropertyHandles::Handles() const
{
    std::vector<Handle> handles;
    if (Workspace.Selection.GetSelection().size() != 1 || !Components.Entity().IsValid())
        return handles;
    for (const PropertyNode& component : Components.Nodes())
        for (const PropertyNode& field : component.Children)
        {
            const std::optional<PropertyHandleKind> kind = KindOf(field.Key);
            float value = 0.0f;
            if (!kind.has_value() || field.Descriptor.ReadOnly
                || std::from_chars(field.Value.data(), field.Value.data() + field.Value.size(), value).ec != std::errc{})
                continue;
            handles.push_back({ .Key = field.Key, .Label = field.Descriptor.Label, .Kind = *kind, .Value = value });
        }
    return handles;
}

std::optional<Vec3d> PropertyHandles::Centre() const
{
    const Transform3f* transform = Workspace.World.FocusDocument().GetScene().TryGetWorldTransform(Components.Entity());
    return transform != nullptr ? std::optional<Vec3d>(transform->Position) : std::nullopt;
}

std::optional<PropertyHandles::Handle> PropertyHandles::KnobAt(Vec2d pointer, ViewportId id) const
{
    const EditorViewport* viewport = Workspace.Layout.Find(id);
    const std::optional<Vec3d> centre = Centre();
    if (viewport == nullptr || !centre.has_value())
        return std::nullopt;
    const ViewportProjection projection(*viewport);
    const Vec3d right = Facing(*viewport).first;
    for (const Handle& handle : Handles())
        if (handle.Kind == PropertyHandleKind::Radius)
            if (const std::optional<ProjectedPoint> knob = projection.WorldToPixel(*centre + right * handle.Value);
                knob.has_value() && (knob->Pixel - pointer).Magnitude() <= EditorUi::Px(kKnobReachDp))
                return handle;
    return std::nullopt;
}

bool PropertyHandles::OverObject(Vec2d pointer, ViewportId id) const
{
    const EditorViewport* viewport = Workspace.Layout.Find(id);
    const std::optional<Vec3d> centre = Centre();
    if (viewport == nullptr || !centre.has_value())
        return false;
    const std::optional<ProjectedPoint> at = ViewportProjection(*viewport).WorldToPixel(*centre);
    return at.has_value() && (at->Pixel - pointer).Magnitude() <= EditorUi::Px(kObjectReachDp);
}

void PropertyHandles::Publish()
{
    PropertyHandleMarks& marks = Workspace.Interaction.Overlay.Handles;
    marks.Clear();
    if (Now == Phase::Scrolling && ++RestingFrames >= kRestFrames)
        Finish();
    const std::vector<Handle> handles = Handles();
    const std::optional<Vec3d> centre = Centre();
    // What was being changed went away under it: nothing to keep.
    if (Now != Phase::Resting && (Components.Entity() != Editing || !centre.has_value()))
        Cancel(nullptr);
    if (handles.empty() || !centre.has_value())
        return;

    const std::optional<Handle> hot = Now == Phase::Resting ? KnobAt(Pointer, PointerView) : std::nullopt;
    for (const std::unique_ptr<EditorViewport>& viewport : Workspace.Layout.All())
    {
        if (!Shown(*viewport))
            continue;
        const auto [right, up] = Facing(*viewport);
        for (const Handle& handle : handles)
        {
            if (handle.Kind != PropertyHandleKind::Radius)
                continue;
            const bool active = (Now == Phase::Dragging && handle.Key == ActiveKey)
                             || (hot.has_value() && hot->Key == handle.Key && PointerView == viewport->Id);
            const Vec4 colour = active ? EditorTheme::DialRimHot : EditorTheme::DialRim;
            Vec3d previous = *centre + right * handle.Value;
            for (int i = 1; i <= kRingSegments; ++i)
            {
                const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kRingSegments;
                const Vec3d next = *centre + (right * std::cos(angle) + up * std::sin(angle)) * handle.Value;
                marks.Rings.push_back({ .From = previous, .To = next, .Color = colour, .Thickness = 1.0f,
                                        .Viewport = viewport->Id });
                previous = next;
            }
            marks.Knobs.push_back({ .World = *centre + right * handle.Value, .Hot = active, .Viewport = viewport->Id });
            if (active)
                marks.Values.push_back({ .World = *centre + right * handle.Value, .Color = EditorTheme::DialRimHot,
                                         .Text = handle.Label + "  " + Text(handle.Value) });
        }
    }
    if (Now == Phase::Scrolling)
        marks.Values.push_back({ .World = *centre, .Color = EditorTheme::DialRimHot,
                                 .Text = ActiveLabel + "  " + Text(ActiveValue) });
}

InputConsumed PropertyHandles::OnInput(const InputEvent& event, PointerCapture& capture)
{
    if (const auto* move = std::get_if<PointerMoveEvent>(&event))
    {
        Pointer = move->Position;
        PointerView = move->Viewport.IsValid() ? move->Viewport : Workspace.Layout.ResolveAt(move->Position);
        if (Now != Phase::Dragging)
            return InputConsumed::No;
        const EditorViewport* viewport = Workspace.Layout.Find(PointerView);
        const std::optional<Vec3d> centre = Centre();
        if (viewport == nullptr || !centre.has_value())
            return InputConsumed::Yes;
        const ViewportProjection projection(*viewport);
        if (const std::optional<ProjectedPoint> at = projection.WorldToPixel(*centre))
        {
            ActiveValue = (Pointer - at->Pixel).Magnitude() * projection.WorldSizeForPixels(*centre, 1.0f);
            (void)Session.Preview(Text(ActiveValue));
        }
        return InputConsumed::Yes;
    }
    if (const auto* down = std::get_if<PointerDownEvent>(&event))
    {
        if (Now == Phase::Scrolling)
            Finish();
        if (down->Button != MouseButton::Left)
            return InputConsumed::No;
        const ViewportId view = down->Viewport.IsValid() ? down->Viewport : Workspace.Layout.ResolveAt(down->Position);
        const std::optional<Handle> knob = KnobAt(down->Position, view);
        if (!knob.has_value() || !Session.Begin(Components, knob->Key))
            return InputConsumed::No;
        Now = Phase::Dragging;
        Editing = Components.Entity();
        ActiveKey = knob->Key;
        ActiveLabel = knob->Label;
        ActiveValue = knob->Value;
        PointerView = view;
        capture.Acquire(PointerCaptureKind::Exclusive, view);
        return InputConsumed::Yes;
    }
    if (const auto* up = std::get_if<PointerUpEvent>(&event))
    {
        if (Now != Phase::Dragging || up->Button != MouseButton::Left)
            return InputConsumed::No;
        Finish();
        if (capture.HeldBySelf())
            capture.Release();
        return InputConsumed::Yes;
    }
    if (const auto* wheel = std::get_if<WheelEvent>(&event))
    {
        if (Now == Phase::Dragging)
            return InputConsumed::Yes;
        const ViewportId view = Workspace.Layout.ResolveAt(Pointer);
        if (!OverObject(Pointer, view) && !KnobAt(Pointer, view).has_value())
            return InputConsumed::No;
        Scroll(wheel->Delta, wheel->Modifiers.Shift);
        return Now == Phase::Scrolling ? InputConsumed::Yes : InputConsumed::No;
    }
    if (const auto* key = std::get_if<KeyDownEvent>(&event))
    {
        if (Now == Phase::Resting)
            return InputConsumed::No;
        if (key->Key == SDLK_ESCAPE)
        {
            Cancel(&capture);
            return InputConsumed::Yes;
        }
        if (Now == Phase::Scrolling)
            Finish();
        return InputConsumed::No;
    }
    if (std::get_if<FocusLostEvent>(&event))
        Cancel(&capture);
    return InputConsumed::No;
}

void PropertyHandles::Scroll(float notches, bool fine)
{
    if (Now == Phase::Resting)
    {
        const std::vector<Handle> handles = Handles();
        const auto scroll = std::ranges::find(handles, PropertyHandleKind::Scroll, &Handle::Kind);
        if (scroll == handles.end() || !Session.Begin(Components, scroll->Key))
            return;
        Now = Phase::Scrolling;
        Editing = Components.Entity();
        ActiveKey = scroll->Key;
        ActiveLabel = scroll->Label;
        ActiveValue = scroll->Value;
    }
    const float step = std::max(std::abs(ActiveValue) * (fine ? 0.01f : 0.1f), fine ? 0.001f : 0.01f);
    ActiveValue = std::max(0.0f, ActiveValue + notches * step);
    (void)Session.Preview(Text(ActiveValue));
    RestingFrames = 0;
}

void PropertyHandles::Finish()
{
    if (Session.IsOpen())
        (void)Session.Commit(Text(ActiveValue));
    Now = Phase::Resting;
}

void PropertyHandles::Cancel(PointerCapture* capture)
{
    Session.Cancel();
    if (Now == Phase::Dragging && capture != nullptr && capture->HeldBySelf())
        capture->Release();
    Now = Phase::Resting;
}
