#pragma once

#include "input/InputEvent.h"
#include "input/InputRouter.h"
#include "properties/PropertyEditSession.h"
#include "viewport/ViewportId.h"

#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ComponentPropertySource;
class EditorWorkspace;
class PointerCapture;

// How a property is changed on the object rather than in a sheet: a size in
// the world as a ring around it with a knob to drag, or a number with no place
// of its own by scrolling over it.
enum class PropertyHandleKind : std::uint8_t
{
    Radius,
    Scroll,
};

// Which component fields have a handle, by the schema's component and field
// names. A table rather than a branch per component: a new handle is a row.
struct PropertyHandleRule
{
    std::string_view Component;
    std::string_view Field;
    PropertyHandleKind Kind;
};

//=============================================================================
// PropertyHandles
//
// The selected object's handles. A Radius is a ring facing each view with a
// knob on its right; dragging the knob sets the radius to the pointer's
// distance from the object, previewed live and committed as one step when let
// go. A Scroll changes its number while the wheel turns over the object or a
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
    };

    [[nodiscard]] std::vector<Handle> Handles() const;
    [[nodiscard]] std::optional<Vec3d> Centre() const;
    // The radius handle whose knob is under `pointer` in `viewport`, if any.
    [[nodiscard]] std::optional<Handle> KnobAt(Vec2d pointer, ViewportId viewport) const;
    [[nodiscard]] bool OverObject(Vec2d pointer, ViewportId viewport) const;
    void Scroll(float notches, bool fine);
    void Finish();
    void Cancel(PointerCapture* capture);

    EditorWorkspace& Workspace;
    ComponentPropertySource& Components;
    PropertyEditSession Session;
    Phase Now = Phase::Resting;
    EntityId Editing{};
    std::string ActiveKey;
    std::string ActiveLabel;
    float ActiveValue = 0.0f;
    int RestingFrames = 0;
    Vec2d Pointer = {};
    ViewportId PointerView = {};
};
