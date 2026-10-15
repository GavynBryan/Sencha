#include "ui/RadialMenuScreen.h"

#include "ui/DocumentLengths.h"

#include "icons/IconNames.h"
#include "tools/RadialMenuFace.h"

#include <math/Angle.h>
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
const UiModelPropertyId kCaptionLeft = UiPropertyIdAt(3);
const UiModelPropertyId kCaptionTop = UiPropertyIdAt(4);
const UiModelPropertyId kCaptionWidth = UiPropertyIdAt(5);
const UiModelPropertyId kCaptionHeight = UiPropertyIdAt(6);
const UiModelPropertyId kHubLeft = UiPropertyIdAt(7);
const UiModelPropertyId kHubTop = UiPropertyIdAt(8);
const UiModelPropertyId kHubSize = UiPropertyIdAt(9);
const UiModelRowsId kSectors = UiRowsIdAt(0);
const UiModelRowsId kKeys = UiRowsIdAt(1);
const UiModelRowsId kWords = UiRowsIdAt(2);
const UiModelRowsId kSummary = UiRowsIdAt(3);

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://wheel.rml";
    desc.ModelName = "wheel";
    desc.Properties = {
        UiModelProperty{ "open", UiValue(false) },
        UiModelProperty{ "caption", UiValue(std::string{}) },
        UiModelProperty{ "caption_dim", UiValue(false) },
        UiModelProperty{ "caption_left", UiValue(std::string("0px")) },
        UiModelProperty{ "caption_top", UiValue(std::string("0px")) },
        UiModelProperty{ "caption_width", UiValue(std::string("0px")) },
        UiModelProperty{ "caption_height", UiValue(std::string("0px")) },
        UiModelProperty{ "hub_left", UiValue(std::string("0px")) },
        UiModelProperty{ "hub_top", UiValue(std::string("0px")) },
        UiModelProperty{ "hub_size", UiValue(std::string("0px")) },
    };
    desc.RowLists = { "sectors", "keys", "words", "summary" };
    return desc;
}

// A circle as the document places one: left, top, diameter.
std::vector<std::string> Circle(Vec2d centre, float radius)
{
    return { PixelLength(centre.X - radius), PixelLength(centre.Y - radius), PixelLength(radius * 2.0f) };
}

std::string_view PartName(RadialMenuPart part)
{
    switch (part)
    {
    case RadialMenuPart::Plate:    return "plate";
    case RadialMenuPart::Rim:      return "rim";
    case RadialMenuPart::Petal:    return "petal";
    case RadialMenuPart::HotPetal: return "hot";
    case RadialMenuPart::Mark:     return "mark";
    case RadialMenuPart::Detail:   return "detail";
    case RadialMenuPart::Hub:      return "hub";
    }
    return "plate";
}

// parts: left, top and diameter of the outer circle, the angle the sector
// begins at and how far it sweeps in degrees clockwise from straight up, and
// its inner radius as a fraction of the outer.
UiRow Sector(Vec2d centre, const RadialMenuSector& sector)
{
    UiRow row{ "", "", std::string(PartName(sector.Part)) };
    row.Parts = { PixelLength(centre.X - sector.Outer), PixelLength(centre.Y - sector.Outer), PixelLength(sector.Outer * 2.0f),
                  std::format("{:.3f}", ToDegrees(sector.Begin)), std::format("{:.3f}", ToDegrees(sector.Width)),
                  std::format("{:.4f}", sector.Outer > 0.0f ? sector.Inner / sector.Outer : 0.0f) };
    return row;
}
} // namespace

RadialMenuScreen::RadialMenuScreen(UiService& ui)
    : Ui(ui)
    , Screen(ui, Describe)
{
}

void RadialMenuScreen::SetShown(bool shown)
{
    Shown = shown;
    if (!shown)
        Screen.Close();
}

void RadialMenuScreen::Present(const RadialMenuSession* wheel, const IRadialMenuModel* menu)
{
    const RadialMenuFace face = wheel != nullptr && menu != nullptr ? FaceOf(*wheel, *menu) : RadialMenuFace{};
    // Open only while a wheel is: the shell and the workspace each have a
    // screen on the window's overlay, and one document model each would clash.
    if (!face.Open || !Shown)
    {
        Screen.Close();
        return;
    }
    if (!Screen.Open())
        return;
    const UiScreenHandle screen = Screen.Handle();
    (void)Ui.SetValue(screen, kOpen, UiValue(face.Open));

    std::vector<UiRow> sectors;
    std::vector<UiRow> keys;
    std::vector<UiRow> words;
    std::vector<UiRow> summary;
    if (face.Open)
    {
        for (const RadialMenuSector& sector : face.Sectors)
            sectors.push_back(Sector(face.Centre, sector));
        const float width = face.PlateRadius * 2.0f;
        (void)Ui.SetValue(screen, kCaptionLeft, UiValue(PixelLength(face.Centre.X - width * 0.5f)));
        (void)Ui.SetValue(screen, kCaptionTop, UiValue(PixelLength(face.CaptionTop)));
        (void)Ui.SetValue(screen, kCaptionWidth, UiValue(PixelLength(width)));
        (void)Ui.SetValue(screen, kCaptionHeight, UiValue(PixelLength(face.CaptionHeight)));

        for (const RadialMenuKey& key : face.Keys)
        {
            UiRow row{ key.Label, key.Active ? "active" : "", key.Variant ? "variant" : "entry" };
            row.Icon = std::string(IconName(key.Icon));
            row.Selected = key.Hot;
            if (key.Word)
            {
                // A word takes the width of its petal rather than its glyph's well.
                const float wordWidth = key.Radius * 3.4f;
                const float wordHeight = key.Radius * 1.8f;
                row.Parts = { PixelLength(key.Centre.X - wordWidth * 0.5f), PixelLength(key.Centre.Y - wordHeight * 0.5f), PixelLength(wordWidth),
                              PixelLength(wordHeight), key.Image };
                words.push_back(std::move(row));
                continue;
            }
            row.Parts = Circle(key.Centre, key.Radius);
            keys.push_back(std::move(row));
        }
        // The hub's readout sits in the square inside its circle.
        const float side = face.HubRadius * 1.5f;
        (void)Ui.SetValue(screen, kHubLeft, UiValue(PixelLength(face.Centre.X - side * 0.5f)));
        (void)Ui.SetValue(screen, kHubTop, UiValue(PixelLength(face.Centre.Y - side * 0.5f)));
        (void)Ui.SetValue(screen, kHubSize, UiValue(PixelLength(side)));
        for (const std::string& line : face.Summary)
            summary.push_back(UiRow{ line, "", "" });
    }
    (void)Ui.SetRows(screen, kSectors, sectors);
    (void)Ui.SetRows(screen, kKeys, keys);
    (void)Ui.SetRows(screen, kWords, words);
    (void)Ui.SetRows(screen, kSummary, summary);
    (void)Ui.SetValue(screen, kCaption, UiValue(face.Caption));
    (void)Ui.SetValue(screen, kCaptionDim, UiValue(face.CaptionNamesCurrent));
}
