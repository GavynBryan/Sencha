#include "workspace/PropertyHandles.h"

#include "EditorTheme.h"
#include "authoring/ComponentPropertySource.h"
#include "authoring/RuntimeFieldText.h"
#include "document/DocumentSerialization.h"
#include "ui/EditorUiStyle.h"
#include "viewport/ViewportProjection.h"
#include "workspace/EditorWorkspace.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <span>

#include <world/serialization/ComponentSerializerRegistry.h>

namespace
{
constexpr PropertyHandleRule kRules[] = {
    { "PointLight", "range", PropertyHandleKind::Radius },
    { "PointLight", "intensity", PropertyHandleKind::Scroll },
    { "SpotLight", "range", PropertyHandleKind::Radius },
    { "SpotLight", "outer_angle_degrees", PropertyHandleKind::Cone, "range" },
    { "SpotLight", "inner_angle_degrees", PropertyHandleKind::Cone, "range" },
    { "SpotLight", "intensity", PropertyHandleKind::Scroll },
    { "Camera", "fov_y_radians", PropertyHandleKind::Spread },
};

// Frames without a wheel turn after which a scroll is one step.
constexpr int kRestFrames = 20;
constexpr int kRingSegments = 64;
constexpr int kArcSegments = 16;
// How near the pointer must be, in design pixels, to take a knob or to scroll
// over the object.
constexpr float kKnobReachDp = 12.0f;
constexpr float kObjectReachDp = 24.0f;
// How far an angle with no reach of its own draws its edges, and where its arc
// sits along them.
constexpr float kEdgeDp = 96.0f;
constexpr float kArcAlongEdge = 0.35f;
constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

const RuntimeField* FieldNamed(const IComponentSerializer& serializer, std::string_view name)
{
    const std::span<const RuntimeField> fields = serializer.RuntimeFields();
    const auto found = std::ranges::find(fields, name, &RuntimeField::Name);
    return found != fields.end() ? &*found : nullptr;
}

bool IsAngle(PropertyHandleKind kind)
{
    return kind == PropertyHandleKind::Cone || kind == PropertyHandleKind::Spread;
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

// An angle about an axis, laid in one plane: its edges leave the apex either
// side of the axis and end at its rim.
struct AngleFan
{
    Vec3d Apex;
    Vec3d Axis;
    Vec3d Across;
    float HalfRadians = 0.0f;
    float Length = 0.0f;

    [[nodiscard]] Vec3d At(float radians, float length) const
    {
        return Apex + (Axis * std::cos(radians) + Across * std::sin(radians)) * length;
    }
    [[nodiscard]] Vec3d Rim(float side) const { return At(HalfRadians * side, Length); }
};

AngleFan FanOf(PropertyHandleKind kind, float degrees, float reach, Vec3d centre, Vec3d forward, Vec3d up,
               const EditorViewport& viewport)
{
    const Vec3d axis = forward.Normalized();
    Vec3d across = kind == PropertyHandleKind::Spread ? up - axis * axis.Dot(up)
                                                      : axis.Cross(viewport.Camera.GetForwardVector());
    if (across.SqrMagnitude() < 1e-6f)
        across = Facing(viewport).first;
    const float half = (kind == PropertyHandleKind::Spread ? degrees * 0.5f : degrees) * kRadiansPerDegree;
    const float length = reach > 0.0f ? reach
                                      : ViewportProjection(viewport).WorldSizeForPixels(centre, EditorUi::Px(kEdgeDp));
    return { .Apex = centre, .Axis = axis, .Across = across.Normalized(), .HalfRadians = half, .Length = length };
}

// The angle, in the handle's degrees, of where `ray` crosses the fan's plane.
std::optional<float> DegreesAt(PropertyHandleKind kind, const AngleFan& fan, const Ray3d& ray)
{
    const Vec3d normal = fan.Axis.Cross(fan.Across);
    const float facing = ray.Direction.Dot(normal);
    if (std::abs(facing) < 1e-4f)
        return std::nullopt;
    const float along = (fan.Apex - ray.Origin).Dot(normal) / facing;
    if (along < 0.0f)
        return std::nullopt;
    const Vec3d offset = ray.Origin + ray.Direction * along - fan.Apex;
    const float degrees = std::atan2(std::abs(offset.Dot(fan.Across)), offset.Dot(fan.Axis)) / kRadiansPerDegree;
    return kind == PropertyHandleKind::Spread ? std::clamp(degrees * 2.0f, 1.0f, 179.0f)
                                              : std::clamp(degrees, 1.0f, 89.0f);
}

bool Shown(const EditorViewport& viewport)
{
    return viewport.RegionMax.X > viewport.RegionMin.X && viewport.RegionMax.Y > viewport.RegionMin.Y;
}

std::string Text(float value)
{
    return std::format("{:.2f}", value);
}

std::string Readout(std::string_view label, PropertyHandleKind kind, float value)
{
    return IsAngle(kind) ? std::format("{}  {:.1f}\xC2\xB0", label, value) : std::format("{}  {:.2f}", label, value);
}
} // namespace

PropertyHandles::PropertyHandles(EditorWorkspace& workspace, ComponentPropertySource& components)
    : Workspace(workspace)
    , Components(components)
    , Pending(workspace.RegisterPendingEdit({ [this] { return Now != Phase::Resting; }, [this] { Finish(); },
                                              [this] { Cancel(); } }))
{
    for (const PropertyHandleRule& rule : kRules)
    {
        const IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(rule.Component);
        const RuntimeField* field = serializer != nullptr ? FieldNamed(*serializer, rule.Field) : nullptr;
        if (field == nullptr || field->ReadOnly || !IsRuntimeFieldEditable(*field))
            continue;
        Rules.push_back({ .Serializer = serializer,
                          .Field = field,
                          .Reach = rule.Reach.empty() ? nullptr : FieldNamed(*serializer, rule.Reach),
                          .Kind = rule.Kind,
                          .Key = components.KeyOf(serializer->TypeId(), rule.Field),
                          .Label = RuntimeFieldLabel(*field) });
    }
}

std::vector<PropertyHandles::Handle> PropertyHandles::Handles() const
{
    std::vector<Handle> handles;
    const EntityId entity = Components.Entity();
    if (Workspace.Selection.GetSelection().size() != 1 || !entity.IsValid())
        return handles;
    const World& world = Workspace.World.FocusDocument().GetScene().GetRegistry().Components;
    float side = 1.0f;
    const IComponentSerializer* component = nullptr;
    for (const ResolvedRule& rule : Rules)
    {
        const ComponentId id = world.GetComponentIdByType(rule.Serializer->TypeId());
        const void* bytes = id != InvalidComponentId ? world.GetComponentRaw(entity, id) : nullptr;
        const std::optional<double> value = RuntimeFieldNumber(*rule.Field, bytes);
        if (!value.has_value())
            continue;
        // Two angles of one object keep their knobs on opposite edges.
        if (rule.Serializer != component)
            side = 1.0f;
        component = rule.Serializer;
        Handle handle{ .Key = rule.Key, .Label = rule.Label, .Kind = rule.Kind, .Value = static_cast<float>(*value) };
        if (IsAngle(rule.Kind))
        {
            handle.Side = side;
            side = -side;
        }
        if (rule.Reach != nullptr)
            handle.Reach = static_cast<float>(RuntimeFieldNumber(*rule.Reach, bytes).value_or(0.0));
        handles.push_back(std::move(handle));
    }
    return handles;
}

std::optional<PropertyHandles::Frame> PropertyHandles::ObjectFrame() const
{
    const Transform3f* transform = Workspace.World.FocusDocument().GetScene().TryGetWorldTransform(Components.Entity());
    if (transform == nullptr)
        return std::nullopt;
    return Frame{ .Centre = transform->Position, .Forward = transform->Forward(), .Up = transform->Up() };
}

std::optional<Vec3d> PropertyHandles::KnobOf(const Handle& handle, const Frame& frame,
                                             const EditorViewport& viewport)
{
    if (handle.Kind == PropertyHandleKind::Radius)
        return frame.Centre + Facing(viewport).first * handle.Value;
    if (IsAngle(handle.Kind))
        return FanOf(handle.Kind, handle.Value, handle.Reach, frame.Centre, frame.Forward, frame.Up, viewport)
            .Rim(handle.Side);
    return std::nullopt;
}

std::optional<PropertyHandles::Handle> PropertyHandles::KnobAt(Vec2d pointer, ViewportId id) const
{
    const EditorViewport* viewport = Workspace.Layout.Find(id);
    const std::optional<Frame> frame = ObjectFrame();
    if (viewport == nullptr || !frame.has_value())
        return std::nullopt;
    const ViewportProjection projection(*viewport);
    for (const Handle& handle : Handles())
        if (const std::optional<Vec3d> knob = KnobOf(handle, *frame, *viewport))
            if (const std::optional<ProjectedPoint> at = projection.WorldToPixel(*knob);
                at.has_value() && (at->Pixel - pointer).Magnitude() <= EditorUi::Px(kKnobReachDp))
                return handle;
    return std::nullopt;
}

bool PropertyHandles::OverObject(Vec2d pointer, ViewportId id) const
{
    const EditorViewport* viewport = Workspace.Layout.Find(id);
    const std::optional<Frame> frame = ObjectFrame();
    if (viewport == nullptr || !frame.has_value())
        return false;
    const std::optional<ProjectedPoint> at = ViewportProjection(*viewport).WorldToPixel(frame->Centre);
    return at.has_value() && (at->Pixel - pointer).Magnitude() <= EditorUi::Px(kObjectReachDp);
}

std::optional<float> PropertyHandles::ValueAt(const Handle& handle, const Frame& frame,
                                              const EditorViewport& viewport, Vec2d pointer)
{
    const ViewportProjection projection(viewport);
    if (IsAngle(handle.Kind))
        return DegreesAt(handle.Kind,
                         FanOf(handle.Kind, handle.Value, handle.Reach, frame.Centre, frame.Forward, frame.Up, viewport),
                         projection.RayThroughPixel(pointer));
    const std::optional<ProjectedPoint> at = projection.WorldToPixel(frame.Centre);
    if (!at.has_value())
        return std::nullopt;
    return (pointer - at->Pixel).Magnitude() * projection.WorldSizeForPixels(frame.Centre, 1.0f);
}

void PropertyHandles::Publish()
{
    PropertyHandleMarks& marks = Workspace.Interaction.Overlay.Handles;
    marks.Clear();
    if (Now == Phase::Scrolling && ++RestingFrames >= kRestFrames)
        Finish();
    const std::vector<Handle> handles = Handles();
    const std::optional<Frame> frame = ObjectFrame();
    // What was being changed went away under it: nothing to keep.
    if (Now != Phase::Resting && (Components.Entity() != Editing || !frame.has_value()))
        Cancel();
    if (handles.empty() || !frame.has_value())
        return;

    const std::optional<Handle> hot = Now == Phase::Resting ? KnobAt(Pointer, PointerView) : std::nullopt;
    for (const std::unique_ptr<EditorViewport>& viewport : Workspace.Layout.All())
    {
        if (!Shown(*viewport))
            continue;
        const auto [right, up] = Facing(*viewport);
        for (const Handle& handle : handles)
        {
            const std::optional<Vec3d> knob = KnobOf(handle, *frame, *viewport);
            if (!knob.has_value())
                continue;
            const bool active = (Now == Phase::Dragging && handle.Key == Active.Key)
                             || (hot.has_value() && hot->Key == handle.Key && PointerView == viewport->Id);
            const Vec4 colour = active ? EditorTheme::DialRimHot : EditorTheme::DialRim;
            const auto stroke = [&](Vec3d from, Vec3d to) {
                marks.Strokes.push_back(
                    { .From = from, .To = to, .Color = colour, .Thickness = 1.0f, .Viewport = viewport->Id });
            };
            if (handle.Kind == PropertyHandleKind::Radius)
            {
                Vec3d previous = *knob;
                for (int i = 1; i <= kRingSegments; ++i)
                {
                    const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kRingSegments;
                    const Vec3d next = frame->Centre + (right * std::cos(angle) + up * std::sin(angle)) * handle.Value;
                    stroke(previous, next);
                    previous = next;
                }
            }
            else
            {
                const AngleFan fan =
                    FanOf(handle.Kind, handle.Value, handle.Reach, frame->Centre, frame->Forward, frame->Up, *viewport);
                stroke(fan.Apex, fan.Rim(1.0f));
                stroke(fan.Apex, fan.Rim(-1.0f));
                const float arc = fan.Length * kArcAlongEdge;
                Vec3d previous = fan.At(-fan.HalfRadians, arc);
                for (int i = 1; i <= kArcSegments; ++i)
                {
                    const Vec3d next =
                        fan.At(fan.HalfRadians * (2.0f * static_cast<float>(i) / kArcSegments - 1.0f), arc);
                    stroke(previous, next);
                    previous = next;
                }
            }
            marks.Knobs.push_back({ .World = *knob, .Hot = active, .Viewport = viewport->Id });
            if (active)
                marks.Values.push_back(
                    { .World = *knob, .Color = EditorTheme::DialRimHot, .Text = Readout(handle.Label, handle.Kind, handle.Value) });
        }
    }
    if (Now == Phase::Scrolling)
        marks.Values.push_back({ .World = frame->Centre, .Color = EditorTheme::DialRimHot,
                                 .Text = Readout(Active.Label, Active.Kind, Active.Value) });
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
        const std::optional<Frame> frame = ObjectFrame();
        if (viewport == nullptr || !frame.has_value())
            return InputConsumed::Yes;
        if (const std::optional<float> value = ValueAt(Active, *frame, *viewport, Pointer))
        {
            Active.Value = *value;
            (void)Session.Preview(Text(Active.Value));
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
        Active = *knob;
        PointerView = view;
        capture.Acquire(PointerCaptureKind::Exclusive, view);
        Captured.emplace(capture);
        return InputConsumed::Yes;
    }
    if (const auto* up = std::get_if<PointerUpEvent>(&event))
    {
        if (Now != Phase::Dragging || up->Button != MouseButton::Left)
            return InputConsumed::No;
        Finish();
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
            Cancel();
            return InputConsumed::Yes;
        }
        if (Now == Phase::Scrolling)
            Finish();
        return InputConsumed::No;
    }
    if (std::get_if<FocusLostEvent>(&event))
        Cancel();
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
        Active = *scroll;
    }
    const float step = std::max(std::abs(Active.Value) * (fine ? 0.01f : 0.1f), fine ? 0.001f : 0.01f);
    Active.Value = std::max(0.0f, Active.Value + notches * step);
    (void)Session.Preview(Text(Active.Value));
    RestingFrames = 0;
}

void PropertyHandles::Finish()
{
    if (Session.IsOpen())
        (void)Session.Commit(Text(Active.Value));
    Rest();
}

void PropertyHandles::Cancel()
{
    Session.Cancel();
    Rest();
}

void PropertyHandles::Rest()
{
    if (Captured.has_value() && Captured->HeldBySelf())
        Captured->Release();
    Captured.reset();
    Now = Phase::Resting;
}
