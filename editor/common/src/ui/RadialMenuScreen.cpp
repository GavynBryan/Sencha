#include "ui/RadialMenuScreen.h"

#include "icons/IconNames.h"
#include "tools/RadialMenuFace.h"

#include <ui/UiService.h>

#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace
{
const UiModelPropertyId kOpen = UiPropertyIdAt(0);
const UiModelPropertyId kCaption = UiPropertyIdAt(1);
const UiModelPropertyId kCaptionDim = UiPropertyIdAt(2);
const UiModelRowsId kDiscs = UiRowsIdAt(0);
const UiModelRowsId kKeys = UiRowsIdAt(1);

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://wheel.rml";
    desc.ModelName = "wheel";
    desc.Properties = {
        UiModelProperty{ "open", UiValue(false) },
        UiModelProperty{ "caption", UiValue(std::string{}) },
        UiModelProperty{ "caption_dim", UiValue(false) },
    };
    desc.RowLists = { "discs", "keys" };
    return desc;
}

std::string Px(float value)
{
    return std::format("{}px", static_cast<int>(std::lround(value)));
}

// A circle as the document places one: left, top, diameter, and the radius
// its corners round by.
std::vector<std::string> Circle(Vec2d centre, float radius)
{
    return { Px(centre.X - radius), Px(centre.Y - radius), Px(radius * 2.0f), Px(radius) };
}

UiRow Disc(std::string kind, Vec2d centre, float radius)
{
    UiRow row{ "", "", std::move(kind) };
    row.Parts = Circle(centre, radius);
    return row;
}

} // namespace

RadialMenuScreen::RadialMenuScreen(UiService& ui)
    : Ui(ui)
{
}

RadialMenuScreen::~RadialMenuScreen()
{
    Close();
}

void RadialMenuScreen::MoveTo(UiSurfaceId overlay)
{
    if (overlay == Surface)
        return;
    Close();
    Surface = overlay;
    if (Shown)
        Open();
}

void RadialMenuScreen::SetShown(bool shown)
{
    Shown = shown;
    if (shown)
        Open();
    else
        Close();
}

void RadialMenuScreen::Open()
{
    if (!Screen.IsValid() && Surface.IsValid())
        Screen = Ui.OpenScreen(Surface, Describe());
}

void RadialMenuScreen::Close()
{
    if (!Screen.IsValid())
        return;
    Ui.CloseScreen(Screen);
    Screen = {};
}

void RadialMenuScreen::Present(const RadialMenuSession* wheel, const IRadialMenuModel* menu)
{
    if (!Screen.IsValid())
        return;
    const RadialMenuFace face = wheel != nullptr && menu != nullptr ? FaceOf(*wheel, *menu) : RadialMenuFace{};
    (void)Ui.SetValue(Screen, kOpen, UiValue(face.Open));

    std::vector<UiRow> discs;
    std::vector<UiRow> keys;
    if (face.Open)
    {
        discs = { Disc("plate", face.Centre, face.PlateRadius), Disc("hub", face.Centre, face.HubRadius) };
        UiRow ledge{ "", "", "caption" };
        const float width = face.PlateRadius * 2.0f;
        ledge.Parts = { Px(face.Centre.X - width * 0.5f), Px(face.CaptionTop), Px(width), Px(face.CaptionHeight) };
        discs.push_back(std::move(ledge));

        for (const RadialMenuKey& key : face.Keys)
        {
            UiRow row{ key.Label, key.Active ? "active" : "", key.Variant ? "variant" : "entry" };
            row.Icon = std::string(IconName(key.Icon));
            row.Selected = key.Hot;
            row.Parts = Circle(key.Centre, key.Radius);
            keys.push_back(std::move(row));
        }
    }
    (void)Ui.SetRows(Screen, kDiscs, discs);
    (void)Ui.SetRows(Screen, kKeys, keys);
    (void)Ui.SetValue(Screen, kCaption, UiValue(face.Caption));
    (void)Ui.SetValue(Screen, kCaptionDim, UiValue(face.CaptionNamesCurrent));
}
