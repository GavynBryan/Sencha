#include "ui/WorldMapSurface.h"

#include <ui/UiService.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <vector>

namespace
{
const UiModelPropertyId kGroundLeft = UiPropertyIdAt(0);
const UiModelPropertyId kGroundTop = UiPropertyIdAt(1);
const UiModelPropertyId kGroundWidth = UiPropertyIdAt(2);
const UiModelPropertyId kGroundHeight = UiPropertyIdAt(3);
const UiModelPropertyId kTitle = UiPropertyIdAt(4);
const UiModelPropertyId kNote = UiPropertyIdAt(5);
const UiModelPropertyId kCameraShown = UiPropertyIdAt(6);
const UiModelPropertyId kCameraLeft = UiPropertyIdAt(7);
const UiModelPropertyId kCameraTop = UiPropertyIdAt(8);
const UiModelRowsId kZones = UiRowsIdAt(0);
const UiModelRowsId kLinks = UiRowsIdAt(1);
const UiModelRowsId kArrows = UiRowsIdAt(2);

constexpr float kDegrees = 180.0f / std::numbers::pi_v<float>;

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://world_map.rml";
    desc.ModelName = "world_map";
    desc.Properties = {
        UiModelProperty{ "ground_left", UiValue(std::string("0px")) },
        UiModelProperty{ "ground_top", UiValue(std::string("0px")) },
        UiModelProperty{ "ground_width", UiValue(std::string("0px")) },
        UiModelProperty{ "ground_height", UiValue(std::string("0px")) },
        UiModelProperty{ "title", UiValue(std::string{}) },
        UiModelProperty{ "note", UiValue(std::string{}) },
        UiModelProperty{ "camera_shown", UiValue(false) },
        UiModelProperty{ "camera_left", UiValue(std::string("0px")) },
        UiModelProperty{ "camera_top", UiValue(std::string("0px")) },
    };
    desc.RowLists = { "zones", "links", "arrows" };
    return desc;
}

std::string Px(float value)
{
    return std::format("{}px", static_cast<int>(std::lround(value)));
}

const char* StateName(WorldMapZoneState state)
{
    switch (state)
    {
    case WorldMapZoneState::Focus: return "focus";
    case WorldMapZoneState::Open:  return "open";
    case WorldMapZoneState::Closed:
    default:                       return "closed";
    }
}

const char* StateText(WorldMapZoneState state)
{
    switch (state)
    {
    case WorldMapZoneState::Focus: return "editing";
    case WorldMapZoneState::Open:  return "open";
    case WorldMapZoneState::Closed:
    default:                       return "not loaded";
    }
}

// A list grows to the longest it has been, rows that show nothing filling it
// out, so a shrinking list never has departing rows evaluated against it.
void Pad(std::vector<UiRow>& rows, std::size_t& longest, std::size_t parts)
{
    longest = std::max(longest, rows.size());
    UiRow hidden{ "", "none", "" };
    hidden.Parts.assign(parts, "0px");
    rows.resize(longest, hidden);
}
} // namespace

WorldMapSurface::WorldMapSurface(UiService& ui)
    : Ui(ui)
{
}

WorldMapSurface::~WorldMapSurface()
{
    Close();
}

void WorldMapSurface::MoveTo(UiSurfaceId content)
{
    if (content == Surface)
        return;
    const bool open = Screen.IsValid();
    Close();
    Surface = content;
    if (open)
        Open();
}

void WorldMapSurface::Open()
{
    if (!Screen.IsValid() && Surface.IsValid())
        Screen = Ui.OpenScreen(Surface, Describe());
}

void WorldMapSurface::Close()
{
    if (!Screen.IsValid())
        return;
    Ui.CloseScreen(Screen);
    Screen = {};
    LongestZones = LongestLinks = LongestArrows = 0;
}

void WorldMapSurface::Present(const WorldMap* map, const Rect2d& ground, const std::string& title,
                              std::optional<ZoneId> hot)
{
    if (map == nullptr)
    {
        Close();
        return;
    }
    Open();
    if (!Screen.IsValid())
        return;
    (void)Ui.SetValue(Screen, kGroundLeft, UiValue(Px(ground.Min().X)));
    (void)Ui.SetValue(Screen, kGroundTop, UiValue(Px(ground.Min().Y)));
    (void)Ui.SetValue(Screen, kGroundWidth, UiValue(Px(ground.Width())));
    (void)Ui.SetValue(Screen, kGroundHeight, UiValue(Px(ground.Height())));
    (void)Ui.SetValue(Screen, kTitle, UiValue(title));
    (void)Ui.SetValue(Screen, kNote,
                      UiValue(map->Unplaced == 0 ? std::string{}
                                                 : std::format("{} zone{} without bounds not shown", map->Unplaced,
                                                               map->Unplaced == 1 ? "" : "s")));
    (void)Ui.SetValue(Screen, kCameraShown, UiValue(map->Camera.has_value()));
    if (map->Camera.has_value())
    {
        (void)Ui.SetValue(Screen, kCameraLeft, UiValue(Px(map->Camera->X)));
        (void)Ui.SetValue(Screen, kCameraTop, UiValue(Px(map->Camera->Y)));
    }

    std::vector<UiRow> zones;
    for (const WorldMapZone& zone : map->Zones)
    {
        UiRow row{ zone.Name, StateName(zone.State), StateText(zone.State) };
        row.Selected = hot.has_value() && *hot == zone.Id;
        row.Parts = { Px(zone.Plate.Min().X), Px(zone.Plate.Min().Y), Px(zone.Plate.Width()), Px(zone.Plate.Height()),
                      zone.Here ? "here" : "", zone.Problem ? "problem" : "" };
        zones.push_back(std::move(row));
    }
    std::vector<UiRow> links;
    std::vector<UiRow> arrows;
    for (const WorldMapLink& link : map->Links)
    {
        const Vec2d along = link.To - link.From;
        const float angle = std::atan2(along.Y, along.X) * kDegrees;
        UiRow row{ "", link.AToB && link.BToA ? "both" : "oneway",
                   link.Failed ? "failed" : link.Warned ? "warned" : "" };
        row.Parts = { Px(link.From.X), Px(link.From.Y), Px(along.Magnitude()),
                      std::format("rotate({:.2f}deg)", angle) };
        links.push_back(row);
        if (link.AToB != link.BToA)
        {
            // The arrow two thirds along, pointing the way the link goes.
            const bool forward = link.AToB;
            const Vec2d at = link.From + along * (forward ? 0.66f : 0.34f);
            UiRow arrow{ "", "oneway", row.Detail };
            arrow.Parts = { Px(at.X), Px(at.Y), "0px", std::format("rotate({:.2f}deg)", forward ? angle : angle + 180.0f) };
            arrows.push_back(std::move(arrow));
        }
    }
    Pad(zones, LongestZones, 6);
    Pad(links, LongestLinks, 4);
    Pad(arrows, LongestArrows, 4);
    (void)Ui.SetRows(Screen, kZones, zones);
    (void)Ui.SetRows(Screen, kLinks, links);
    (void)Ui.SetRows(Screen, kArrows, arrows);
}
