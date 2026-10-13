#include "presenters/PlanePad.h"

#include "ui/DocumentLengths.h"

#include <ui/UiService.h>

#include <algorithm>
#include <format>

namespace
{
const UiModelPropertyId kLeft = UiPropertyIdAt(0);
const UiModelPropertyId kTop = UiPropertyIdAt(1);
const UiModelPropertyId kWidth = UiPropertyIdAt(2);
const UiModelPropertyId kHeight = UiPropertyIdAt(3);
const UiModelPropertyId kCaption = UiPropertyIdAt(4);
const UiModelPropertyId kPointLeft = UiPropertyIdAt(5);
const UiModelPropertyId kPointTop = UiPropertyIdAt(6);
const UiModelRowsId kMarks = UiRowsIdAt(0);

// Kept clear inside the edge, so a mark at either end of a range is whole.
constexpr float kInset = 14.0f;
constexpr float kPointDiameter = 16.0f;

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://plane_pad.rml";
    desc.ModelName = "plane_pad";
    const auto pixels = [](const char* name) { return UiModelProperty{ name, UiValue(std::string("0px")) }; };
    desc.Properties = { pixels("left"),
                        pixels("top"),
                        pixels("width"),
                        pixels("height"),
                        UiModelProperty{ "caption", UiValue(std::string{}) },
                        pixels("point_left"),
                        pixels("point_top") };
    desc.RowLists = { "marks" };
    return desc;
}
} // namespace

Vec2d PlanePadValueAt(const Rect2d& region, bool line, Vec2d windowPoint)
{
    const float width = std::max(1.0f, region.Width() - 2.0f * kInset);
    const float height = std::max(1.0f, region.Height() - 2.0f * kInset);
    const Vec2d local = windowPoint - region.Position;
    return { std::clamp((local.X - kInset) / width, 0.0f, 1.0f),
             line ? 0.5f : std::clamp(1.0f - (local.Y - kInset) / height, 0.0f, 1.0f) };
}

Vec2d PlanePadPointOf(const Rect2d& region, Vec2d at)
{
    return region.Position + Vec2d(kInset + at.X * (region.Width() - 2.0f * kInset),
                                   kInset + (1.0f - at.Y) * (region.Height() - 2.0f * kInset));
}

PlanePad::PlanePad(UiService& ui)
    : Ui(ui)
    , Screen(ui, Describe)
{
}

void PlanePad::Show(const Rect2d& region, std::span<const PlanePadMark> marks, Vec2d point, std::string caption,
                    bool line)
{
    Region = region;
    Line = line;
    Showing = true;
    if (!Screen.Open())
        return;
    const UiScreenHandle screen = Screen.Handle();
    (void)Ui.SetValue(screen, kLeft, UiValue(PixelLength(region.Position.X)));
    (void)Ui.SetValue(screen, kTop, UiValue(PixelLength(region.Position.Y)));
    (void)Ui.SetValue(screen, kWidth, UiValue(PixelLength(region.Width())));
    (void)Ui.SetValue(screen, kHeight, UiValue(PixelLength(region.Height())));
    (void)Ui.SetValue(screen, kCaption, UiValue(std::move(caption)));
    const Vec2d at = PlanePadPointOf(region, point) - region.Position - Vec2d(kPointDiameter * 0.5f, kPointDiameter * 0.5f);
    (void)Ui.SetValue(screen, kPointLeft, UiValue(PixelLength(at.X)));
    (void)Ui.SetValue(screen, kPointTop, UiValue(PixelLength(at.Y)));

    std::vector<UiRow> rows;
    rows.reserve(marks.size());
    for (const PlanePadMark& mark : marks)
    {
        const float diameter = 6.0f + 14.0f * std::clamp(mark.Weight, 0.0f, 1.0f);
        const Vec2d corner = PlanePadPointOf(region, mark.At) - region.Position - Vec2d(diameter * 0.5f, diameter * 0.5f);
        UiRow row{ mark.Label, "", "" };
        row.Selected = mark.Weight > 0.0f;
        row.Parts = { PixelLength(corner.X), PixelLength(corner.Y), PixelLength(diameter) };
        rows.push_back(std::move(row));
    }
    (void)Ui.SetRows(screen, kMarks, rows);
}

void PlanePad::Hide()
{
    Showing = false;
    Screen.Close();
}

bool PlanePad::Contains(Vec2d windowPoint) const
{
    return Showing && Region.Contains(windowPoint);
}
