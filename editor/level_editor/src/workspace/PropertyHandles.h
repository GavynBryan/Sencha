#pragma once

#include "input/InputEvent.h"
#include "input/InputRouter.h"
#include "properties/PropertyEditSession.h"
#include "viewport/ViewportId.h"

#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ComponentPropertySource;
class IComponentSerializer;
struct RuntimeField;
struct EditorViewport;
class EditorWorkspace;

// How a property is changed on the object rather than in a sheet: a size in
// the world as a ring around it with a knob to drag; an angle about the
// object's forward axis as the edges of the cone it opens, with a knob on one
// edge; or a number with no place of its own by scrolling over it. Angles are
// in degrees, as the sheet shows them.
enum class PropertyHandleKind : std::uint8_t
{
    Radius,
    // Rim to axis: the cone is drawn in whichever plane through the axis faces
    // the view.
    Cone,
    // Rim to rim, upward: drawn in the plane of the object's forward and up.
    Spread,
    Scroll,
};

// Which component fields have a handle, by the schema's component and field
// names. A table rather than a branch per component: a new handle is a row.
// An angle's edges reach as far as the `Reach` field of the same component, or
// a fixed size on screen without one.
struct PropertyHandleRule
{
    std::string_view Component;
    std::string_view Field;
    PropertyHandleKind Kind;
    std::string_view Reach = {};
};

//=============================================================================
// PropertyHandles
//
// The selected object's handles. A Radius is a ring facing each view with a
// knob on its right; dragging the knob sets the radius to the pointer's
// distance from the object. An angle's knob sits on one of its edges, and
// dragging it sets the angle to the pointer's, about the axis. Either is
// previewed live and committed as one step when let go. A Scroll changes its number while the wheel turns over the object or a
// knob, committed as one step once the wheel rests. The value shows beside
// the handle only while it changes, or while the pointer is on the knob. Every
// interruption -- Escape, focus loss, the selection moving on -- cancels an
// unfinished change.
//=============================================================================
class PropertyHandles
{
public:
    PropertyHandles(EditorWorkspace& workspace, ComponentPropertySource& components);

    // Per frame: the marks the handles show now, and a scroll that has rested
    // committed.
    void Publish();
    InputConsumed OnInput(const InputEvent& event, PointerCapture& capture);

private:
    enum class Phase : std::uint8_t
    {
        Resting,
        Dragging,
        Scrolling,
    };

    struct Handle
    {
        std::string Key;
        std::string Label;
        PropertyHandleKind Kind = PropertyHandleKind::Radius;
        float Value = 0.0f;
        // How far an angle's edges reach; zero is a fixed size on screen.
        float Reach = 0.0f;
        // Which edge of an angle carries the knob, so two angles of one object
        // keep their knobs apart.
        float Side = 1.0f;
    };

    // A rule found in the schema: the field it reads and the key it edits.
    struct ResolvedRule
    {
        const IComponentSerializer* Serializer = nullptr;
        const RuntimeField* Field = nullptr;
        const RuntimeField* Reach = nullptr;
        PropertyHandleKind Kind = PropertyHandleKind::Radius;
        std::string Key;
        std::string Label;
    };

    struct Frame
    {
        Vec3d Centre;
        Vec3d Forward;
        Vec3d Up;
    };

    [[nodiscard]] std::vector<Handle> Handles() const;
    [[nodiscard]] std::optional<Frame> ObjectFrame() const;
    [[nodiscard]] static std::optional<Vec3d> KnobOf(const Handle& handle, const Frame& frame,
                                                     const EditorViewport& viewport);
    // What dragging `handle`'s knob to `pointer` sets it to.
    [[nodiscard]] static std::optional<float> ValueAt(const Handle& handle, const Frame& frame,
                                                      const EditorViewport& viewport, Vec2d pointer);
    // The handle whose knob is under `pointer` in `viewport`, if any.
    [[nodiscard]] std::optional<Handle> KnobAt(Vec2d pointer, ViewportId viewport) const;
    [[nodiscard]] bool OverObject(Vec2d pointer, ViewportId viewport) const;
    void Scroll(float notches, bool fine);
    void Finish();
    void Cancel();
    // Back to resting, letting go of the pointer if a drag held it.
    void Rest();

    EditorWorkspace& Workspace;
    ComponentPropertySource& Components;
    std::vector<ResolvedRule> Rules;
    PropertyEditSession Session;
    Phase Now = Phase::Resting;
    EntityId Editing{};
    // The handle being changed, its value as previewed.
    Handle Active;
    int RestingFrames = 0;
    Vec2d Pointer = {};
    ViewportId PointerView = {};
    // The router's hold on the pointer while a knob is dragged.
    std::optional<PointerCapture> Captured;
    // Last: the workspace stops resolving this edit before anything it touches goes.
    std::shared_ptr<const void> Pending;
};
