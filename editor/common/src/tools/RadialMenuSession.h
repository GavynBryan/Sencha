#pragma once

#include "RadialMenuMath.h"
#include "RadialMenuModel.h"

#include "input/InputEvent.h"
#include "input/KeymapFile.h"

#include <cstdint>
#include <functional>
#include <vector>

class PointerCapture;

// Wheel lifecycle. Closed: the key is up. Open: the key is down and the wheel
// is showing; the layout and the hot sector are meaningful, and this handler
// holds the pointer exclusively. Dismissed: the key is still down but the
// wheel is gone (Escape); it cannot reopen until the key has been released,
// whatever presses the boundary delivers meanwhile.
enum class RadialMenuPhase : std::uint8_t
{
    Closed,
    Open,
    Dismissed,
};

// What opened the wheel, and so what closes it: the key held, whose release
// chooses; or a click, after which the next click chooses -- or, off every
// entry, closes it -- and the key plays no part.
enum class RadialMenuGesture : std::uint8_t
{
    Held,
    Clicked,
};

// The hold-to-choose radial menu: press the key and a wheel of the model's
// entries opens at the pointer; moving picks a sector by direction; releasing
// the key selects that sector's entry on the model. What the entries are (the
// registered tools, the gizmo modes) is the model's business alone. An
// input-router handler, placed after the UI guard (a focused text field keeps
// its letters) and ahead of every viewport handler, so the wheel is modal
// while it is open; two wheels on one router exclude each other by that, the
// first to open swallowing the other's key.
//
// The wheel opens only with the pointer inside a scene viewport, which the
// application answers per press: elsewhere the key is swallowed and nothing
// happens, so the gesture is never half-begun over a panel or a bar.
//
// An entry with variants shows them as a compact fan outside the rim,
// centred on its own petal, while it is hot; a release selects the hot entry
// with its hot variant, or -1. Which entry is hot is RadialMenu::PickAt's
// rule over the pointer, the entry it held, and Anchor: the pointer's last
// report before it set out for that entry's fan. A straight line out to any
// variant keeps the entry, and a line toward another slot or the hub is
// browsing again (docs/editor/interaction-grammar.md, "A wheel's fans are
// reached in a straight line").
//
// The hot sector starts empty at open and is set only by pointer motion: a
// wheel shifted in from a window edge may open with the pointer already
// inside a sector, and an immediate release must not select by accident.
// What a release over the active entry means is the model's rule, not the
// wheel's.
class RadialMenuSession
{
public:
    // `frame` is read once per open: the window area and UI scale the wheel
    // is placed against, which is what keeps hit-testing and painting on one
    // set of numbers.
    RadialMenuSession(IRadialMenuModel& menu, KeyChord key, std::function<RadialMenu::Frame()> frame,
                      std::function<bool(Vec2d pointer)> pointerOnScene, RadialMenu::Proportions proportions = {});

    InputConsumed OnInput(const InputEvent& event, PointerCapture& capture);
    // Opens it at `pointer` for a click to choose from, whatever the key is
    // doing; nothing when the model offers nothing.
    void OpenClicked(Vec2d pointer, PointerCapture& capture);

    [[nodiscard]] RadialMenuPhase GetPhase() const { return Phase; }
    [[nodiscard]] RadialMenuGesture GetGesture() const { return Gesture; }
    // Meaningful while Open.
    [[nodiscard]] const RadialMenu::Layout& GetLayout() const { return Layout; }
    // The sector under the pointer, or -1 (the hub, or no motion yet).
    [[nodiscard]] int GetHot() const { return Hot; }
    // The hot entry's variant under the pointer, or -1 (none, or the entry has none).
    [[nodiscard]] int GetHotVariant() const { return HotVariant; }
    [[nodiscard]] RadialMenu::Hold GetHold() const { return Hold; }

private:
    InputConsumed OnKeyDown(const KeyDownEvent& event, PointerCapture& capture);
    InputConsumed OnKeyUp(const KeyUpEvent& event, PointerCapture& capture);
    void Open(Vec2d pointer, PointerCapture& capture);
    void Leave(RadialMenuPhase next, PointerCapture& capture);
    void Track(Vec2d pointer);
    void Select(int index, int variant);
    [[nodiscard]] bool IsWheelKey(SDL_Keycode key) const { return key == Key.Key; }

    IRadialMenuModel& Menu;
    KeyChord Key;
    std::function<RadialMenu::Frame()> FrameProvider;
    std::function<bool(Vec2d)> PointerOnScene;
    RadialMenu::Proportions Sizes;
    RadialMenuPhase Phase = RadialMenuPhase::Closed;
    RadialMenuGesture Gesture = RadialMenuGesture::Held;
    RadialMenu::Layout Layout{};
    std::vector<int> FanSizes;
    int Hot = -1;
    int HotVariant = -1;
    RadialMenu::Hold Hold = RadialMenu::Hold::None;
    Vec2d Anchor{};
};
