#pragma once

#include "ui/UiScreenSlot.h"

#include "properties/ColorValue.h"
#include "properties/PropertyEditSession.h"

#include <math/Vec.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

class IPropertySource;
class UiService;

// Where a colour sits on a hue and saturation disc of radius 1, y down: hue
// turns clockwise from the top, saturation runs out from the middle.
[[nodiscard]] Vec2d ColorWheelPoint(Hsv color);
// The hue and saturation at `point` on that disc, held to its rim, at `value`.
[[nodiscard]] Hsv ColorWheelPick(Vec2d point, double value);

// The colour property a picker edits, reached through its source each time:
// what it belonged to may have gone since the picker opened.
struct ColorPickerTarget
{
    std::function<IPropertySource*()> Source;
    std::string Key;
    std::string Label;
};

//=============================================================================
// ColorPicker
// A hue/saturation wheel and value bar for one colour property: a drag previews
// through its edit session, release commits, a press elsewhere or closing cancels.
//=============================================================================
class ColorPicker
{
public:
    ColorPicker(UiService& ui, UiSurfaceId surface);
    ~ColorPicker();

    ColorPicker(const ColorPicker&) = delete;
    ColorPicker& operator=(const ColorPicker&) = delete;

    // Opens beside `anchor`, a box in surface pixels.
    void Open(ColorPickerTarget target, std::string_view value, UiElementBox anchor);
    void Close();
    [[nodiscard]] bool IsOpen() const { return Screen.IsOpen(); }
    [[nodiscard]] UiScreenHandle CurrentScreen() const { return Screen.Handle(); }

    // Per frame while open: act on what the document asked for, then publish.
    // False once the person has closed it.
    bool Update();

private:
    enum class Part : std::uint8_t { None, Wheel, Value };

    void Grab(Part part, float x, float y);
    void Move(float x, float y);
    void Release();
    void Type(std::string_view text);
    void Publish();
    [[nodiscard]] IPropertySource* Source() const;

    UiService& Ui;
    UiScreenSlot Screen;
    ColorPickerTarget Target;
    ColorValue Shown;
    Hsv Chosen;
    Part Held = Part::None;
    PropertyEditSession Session;
};
