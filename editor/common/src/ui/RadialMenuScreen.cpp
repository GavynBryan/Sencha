#include "ui/RadialMenuScreen.h"

#include "icons/IconNames.h"
#include "tools/RadialMenuFace.h"

#include <ui/UiService.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <vector>

namespace
{
constexpr float kDegrees = 180.0f / std::numbers::pi_v<float>;

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

std::string Px(float value)
{
    return std::format("{}px", static_cast<int>(std::lround(value)));
}

// A circle as the document places one: left, top, diameter.
std::vector<std::string> Circle(Vec2d centre, float radius)
{
    return { Px(centre.X - radius), Px(centre.Y - radius), Px(radius * 2.0f) };
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
    row.Parts = { Px(centre.X - sector.Outer), Px(centre.Y - sector.Outer), Px(sector.Outer * 2.0f),
                  std::format("{:.3f}", sector.Begin * kDegrees), std::format("{:.3f}", sector.Width * kDegrees),
                  std::format("{:.4f}", sector.Outer > 0.0f ? sector.Inner / sector.Outer : 0.0f) };
    return row;
}

// A list grows to the longest it has been, rows that show nothing filling it
// out: the document engine evaluates a shrinking list's departing rows once
// against the shorter list and warns, every time a petal changes underfoot.
void PadRows(std::vector<UiRow>& rows, std::size_t& longest, const UiRow& hidden)
{
    longest = std::max(longest, rows.size());
    rows.resize(longest, hidden);
}

UiRow HiddenRow(std::vector<std::string> parts)
{
    UiRow row{ "", "none", "none" };
    row.Parts = std::move(parts);
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
    LongestSectors = 0;
    LongestKeys = 0;
    LongestWords = 0;
    LongestSummary = 0;
}

void RadialMenuScreen::Present(const RadialMenuSession* wheel, const IRadialMenuModel* menu)
{
    if (!Screen.IsValid())
        return;
    const RadialMenuFace face = wheel != nullptr && menu != nullptr ? FaceOf(*wheel, *menu) : RadialMenuFace{};
    (void)Ui.SetValue(Screen, kOpen, UiValue(face.Open));

    std::vector<UiRow> sectors;
    std::vector<UiRow> keys;
    std::vector<UiRow> words;
    std::vector<UiRow> summary;
    if (face.Open)
    {
        for (const RadialMenuSector& sector : face.Sectors)
            sectors.push_back(Sector(face.Centre, sector));
        const float width = face.PlateRadius * 2.0f;
        (void)Ui.SetValue(Screen, kCaptionLeft, UiValue(Px(face.Centre.X - width * 0.5f)));
        (void)Ui.SetValue(Screen, kCaptionTop, UiValue(Px(face.CaptionTop)));
        (void)Ui.SetValue(Screen, kCaptionWidth, UiValue(Px(width)));
        (void)Ui.SetValue(Screen, kCaptionHeight, UiValue(Px(face.CaptionHeight)));

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
                row.Parts = { Px(key.Centre.X - wordWidth * 0.5f), Px(key.Centre.Y - wordHeight * 0.5f), Px(wordWidth),
                              Px(wordHeight) };
                words.push_back(std::move(row));
                continue;
            }
            row.Parts = Circle(key.Centre, key.Radius);
            keys.push_back(std::move(row));
        }
        // The hub's readout sits in the square inside its circle.
        const float side = face.HubRadius * 1.5f;
        (void)Ui.SetValue(Screen, kHubLeft, UiValue(Px(face.Centre.X - side * 0.5f)));
        (void)Ui.SetValue(Screen, kHubTop, UiValue(Px(face.Centre.Y - side * 0.5f)));
        (void)Ui.SetValue(Screen, kHubSize, UiValue(Px(side)));
        for (const std::string& line : face.Summary)
            summary.push_back(UiRow{ line, "", "" });
    }
    PadRows(sectors, LongestSectors, HiddenRow({ "0px", "0px", "0px", "0", "0", "0" }));
    PadRows(keys, LongestKeys, HiddenRow({ "0px", "0px", "0px" }));
    PadRows(words, LongestWords, HiddenRow({ "0px", "0px", "0px", "0px" }));
    PadRows(summary, LongestSummary, HiddenRow({}));
    (void)Ui.SetRows(Screen, kSectors, sectors);
    (void)Ui.SetRows(Screen, kKeys, keys);
    (void)Ui.SetRows(Screen, kWords, words);
    (void)Ui.SetRows(Screen, kSummary, summary);
    (void)Ui.SetValue(Screen, kCaption, UiValue(face.Caption));
    (void)Ui.SetValue(Screen, kCaptionDim, UiValue(face.CaptionNamesCurrent));
}
