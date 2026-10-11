#include "presenters/ColorPicker.h"

#include "properties/PropertySource.h"

#include <ui/UiService.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <vector>

namespace
{
constexpr int kRings = 6;
constexpr int kSegments = 36;
constexpr int kValueSteps = 12;
// Neighbouring cells overlap by this much, so no antialiased seam shows the
// plate between them.
constexpr double kSeamDegrees = 0.6;

const UiModelPropertyId kLeft = UiPropertyIdAt(0);
const UiModelPropertyId kRight = UiPropertyIdAt(1);
const UiModelPropertyId kTop = UiPropertyIdAt(2);
const UiModelPropertyId kBottom = UiPropertyIdAt(3);
const UiModelPropertyId kLabel = UiPropertyIdAt(4);
const UiModelPropertyId kText = UiPropertyIdAt(5);
const UiModelPropertyId kSwatch = UiPropertyIdAt(6);
const UiModelPropertyId kMarkLeft = UiPropertyIdAt(7);
const UiModelPropertyId kMarkTop = UiPropertyIdAt(8);
const UiModelPropertyId kValueMark = UiPropertyIdAt(9);
const UiModelRowsId kCells = UiRowsIdAt(0);
const UiModelRowsId kValues = UiRowsIdAt(1);
const UiActionId kGrab = UiActionIdAt(0);
const UiActionId kDrag = UiActionIdAt(1);
const UiActionId kRelease = UiActionIdAt(2);
const UiActionId kTypeAction = UiActionIdAt(3);
const UiActionId kClose = UiActionIdAt(4);

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://color_picker.rml";
    desc.ModelName = "color_picker";
    desc.Properties = {
        UiModelProperty{ "picker_left", UiValue(std::string("auto")) },
        UiModelProperty{ "picker_right", UiValue(std::string("auto")) },
        UiModelProperty{ "picker_top", UiValue(std::string("auto")) },
        UiModelProperty{ "picker_bottom", UiValue(std::string("auto")) },
        UiModelProperty{ "label", UiValue(std::string{}) },
        UiModelProperty{ "value_text", UiValue(std::string{}), true },
        UiModelProperty{ "swatch", UiValue(std::string("rgba(0, 0, 0, 0)")) },
        UiModelProperty{ "mark_left", UiValue(std::string("50%")) },
        UiModelProperty{ "mark_top", UiValue(std::string("50%")) },
        UiModelProperty{ "value_mark", UiValue(std::string("0%")) },
    };
    desc.RowLists = { "cells", "values" };
    desc.Actions = { "color_grab", "color_drag", "color_release", "color_type", "color_close" };
    return desc;
}

std::string Percent(double fraction)
{
    return std::format("{:.3f}%", fraction * 100.0);
}

// The wheel at full value, a ring of cells per saturation band: each cell its
// box (left, top and size as a share of the wheel), its arc, its inner radius
// as a share of its outer, and its colour.
std::vector<UiRow> WheelCells()
{
    std::vector<UiRow> cells;
    cells.reserve(kRings * kSegments);
    for (int ring = 0; ring < kRings; ++ring)
    {
        const double inner = static_cast<double>(ring) / kRings;
        const double outer = static_cast<double>(ring + 1) / kRings;
        for (int segment = 0; segment < kSegments; ++segment)
        {
            const double hue = (segment + 0.5) / kSegments;
            UiRow cell;
            cell.Parts = { Percent((1.0 - outer) * 0.5),
                           Percent((1.0 - outer) * 0.5),
                           Percent(outer),
                           std::format("{:.3f}", segment * 360.0 / kSegments - kSeamDegrees * 0.5),
                           std::format("{:.3f}", 360.0 / kSegments + kSeamDegrees),
                           std::format("{:.4f}", ring == 0 ? 0.0 : inner / outer),
                           ColorSwatch(RgbOf({ hue, (inner + outer) * 0.5, 1.0 })) };
            cells.push_back(std::move(cell));
        }
    }
    return cells;
}
} // namespace

Vec2d ColorWheelPoint(Hsv color)
{
    const double angle = color.H * 2.0 * std::numbers::pi;
    const double radius = std::clamp(color.S, 0.0, 1.0);
    return { static_cast<float>(std::sin(angle) * radius), static_cast<float>(-std::cos(angle) * radius) };
}

Hsv ColorWheelPick(Vec2d point, double value)
{
    const double radius = std::hypot(point.X, point.Y);
    double turns = std::atan2(point.X, -point.Y) / (2.0 * std::numbers::pi);
    if (turns < 0.0)
        turns += 1.0;
    return { .H = turns, .S = std::min(radius, 1.0), .V = value };
}

ColorPicker::ColorPicker(UiService& ui, UiSurfaceId surface)
    : Ui(ui)
    , Surface(surface)
{
}

ColorPicker::~ColorPicker()
{
    Close();
}

void ColorPicker::Open(ColorPickerTarget target, std::string_view value, UiElementBox anchor)
{
    Close();
    Target = std::move(target);
    Shown = ParseColorValue(value).value_or(ColorValue{ .Color = { 1.0, 1.0, 1.0 }, .Rest = {} });
    Chosen = HsvOf(Shown.Color);
    Chosen.V = std::min(Chosen.V, 1.0);
    Screen = Ui.OpenScreen(Surface, Describe());
    if (!Screen.IsValid())
        return;
    // Beside the field, on whichever side of it the window has room.
    const RenderExtent extent = Ui.GetSurfaceSize(Surface);
    const float width = static_cast<float>(extent.Width);
    const float height = static_cast<float>(extent.Height);
    constexpr float kGap = 8.0f;
    const auto px = [](float pixels) { return UiValue(std::format("{:.0f}px", pixels)); };
    if (anchor.X + anchor.Width * 0.5f > width * 0.5f)
        (void)Ui.SetValue(Screen, kRight, px(width - anchor.X + kGap));
    else
        (void)Ui.SetValue(Screen, kLeft, px(anchor.X + anchor.Width + kGap));
    if (anchor.Y + anchor.Height * 0.5f > height * 0.5f)
        (void)Ui.SetValue(Screen, kBottom, px(height - anchor.Y - anchor.Height));
    else
        (void)Ui.SetValue(Screen, kTop, px(anchor.Y));
    (void)Ui.SetValue(Screen, kLabel, UiValue(Target.Label));
    (void)Ui.SetRows(Screen, kCells, WheelCells());
    Publish();
}

void ColorPicker::Close()
{
    Session.Cancel();
    Held = Part::None;
    if (Screen.IsValid())
        Ui.CloseScreen(Screen);
    Screen = {};
}

bool ColorPicker::Update()
{
    if (!IsOpen())
        return false;
    for (const UiAction& action : Ui.DrainActions(Screen))
    {
        const auto argument = [&](std::size_t index) {
            return index < action.Arguments.size() ? static_cast<float>(action.Arguments[index].AsFloat()) : 0.0f;
        };
        if (action.Id == kGrab && !action.Arguments.empty())
            Grab(action.Arguments[0].AsString() == "value" ? Part::Value : Part::Wheel, argument(1), argument(2));
        else if (action.Id == kDrag)
            Move(argument(0), argument(1));
        else if (action.Id == kRelease)
            Release();
        else if (action.Id == kTypeAction && action.Arguments.size() > 1 && action.Arguments[1].AsBool())
            Type(action.Arguments[0].AsString());
        else if (action.Id == kClose)
        {
            Close();
            return false;
        }
    }
    return IsOpen();
}

IPropertySource* ColorPicker::Source() const
{
    return Target.Source ? Target.Source() : nullptr;
}

void ColorPicker::Grab(Part part, float x, float y)
{
    IPropertySource* source = Source();
    if (source == nullptr || (!Session.IsOpen() && !Session.Begin(*source, Target.Key)))
        return;
    Held = part;
    // A hue picked while the colour is black would stay black.
    if (part == Part::Wheel && Chosen.V <= 0.0)
        Chosen.V = 1.0;
    Move(x, y);
}

void ColorPicker::Move(float x, float y)
{
    const std::string_view element = Held == Part::Wheel ? "color_wheel" : "color_value";
    const std::optional<UiElementBox> box =
        Held == Part::None ? std::nullopt : Ui.MeasureElement(Screen, element, UiBoxArea::Content);
    if (!box.has_value() || box->Width <= 0.0f || box->Height <= 0.0f)
        return;
    if (Held == Part::Wheel)
    {
        const Vec2d point{ (x - box->X - box->Width * 0.5f) / (box->Width * 0.5f),
                           (y - box->Y - box->Height * 0.5f) / (box->Height * 0.5f) };
        const Hsv picked = ColorWheelPick(point, Chosen.V);
        // On the middle the hue is undefined; the one already chosen stays.
        Chosen = { .H = picked.S > 0.0 ? picked.H : Chosen.H, .S = picked.S, .V = picked.V };
    }
    else
        Chosen.V = std::clamp(1.0 - (y - box->Y) / box->Height, 0.0, 1.0);
    Shown.Color = RgbOf(Chosen);
    (void)Session.Preview(ColorValueText(Shown));
    Publish();
}

void ColorPicker::Release()
{
    Held = Part::None;
    if (Session.IsOpen())
        (void)Session.Commit(ColorValueText(Shown));
}

void ColorPicker::Type(std::string_view text)
{
    const std::optional<ColorValue> typed = ParseColorValue(text);
    IPropertySource* source = Source();
    if (!typed.has_value() || source == nullptr)
    {
        Publish();
        return;
    }
    Shown = *typed;
    Chosen = HsvOf(Shown.Color);
    Chosen.V = std::min(Chosen.V, 1.0);
    (void)Session.Set(*source, Target.Key, ColorValueText(Shown));
    Publish();
}

void ColorPicker::Publish()
{
    const Vec2d mark = ColorWheelPoint(Chosen);
    (void)Ui.SetValue(Screen, kText, UiValue(ColorValueText(Shown)));
    (void)Ui.SetValue(Screen, kSwatch, UiValue(ColorSwatch(Shown.Color)));
    (void)Ui.SetValue(Screen, kMarkLeft, UiValue(Percent(0.5 + mark.X * 0.5)));
    (void)Ui.SetValue(Screen, kMarkTop, UiValue(Percent(0.5 + mark.Y * 0.5)));
    (void)Ui.SetValue(Screen, kValueMark, UiValue(Percent(1.0 - Chosen.V)));
    std::vector<UiRow> values(kValueSteps);
    for (int step = 0; step < kValueSteps; ++step)
        values[step].Value = ColorSwatch(RgbOf({ Chosen.H, Chosen.S, 1.0 - (step + 0.5) / kValueSteps }));
    (void)Ui.SetRows(Screen, kValues, values);
}
