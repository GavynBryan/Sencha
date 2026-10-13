#include "ui/WorldMapSurface.h"

#include "ui/DocumentLengths.h"

#include <ui/UiService.h>

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
} // namespace

WorldMapSurface::WorldMapSurface(UiService& ui)
    : Ui(ui)
    , Screen(ui, Describe)
{
}

void WorldMapSurface::Present(const WorldMap* map, const Rect2d& ground, const std::string& title,
                              std::optional<ZoneId> hot)
{
    if (map == nullptr)
    {
        Screen.Close();
        return;
    }
    if (!Screen.Open())
        return;
    const UiScreenHandle screen = Screen.Handle();
    (void)Ui.SetValue(screen, kGroundLeft, UiValue(PixelLength(ground.Min().X)));
    (void)Ui.SetValue(screen, kGroundTop, UiValue(PixelLength(ground.Min().Y)));
    (void)Ui.SetValue(screen, kGroundWidth, UiValue(PixelLength(ground.Width())));
    (void)Ui.SetValue(screen, kGroundHeight, UiValue(PixelLength(ground.Height())));
    (void)Ui.SetValue(screen, kTitle, UiValue(title));
    (void)Ui.SetValue(screen, kNote,
                      UiValue(map->Unplaced == 0 ? std::string{}
                                                 : std::format("{} zone{} without bounds not shown", map->Unplaced,
                                                               map->Unplaced == 1 ? "" : "s")));
    (void)Ui.SetValue(screen, kCameraShown, UiValue(map->Camera.has_value()));
    if (map->Camera.has_value())
    {
        (void)Ui.SetValue(screen, kCameraLeft, UiValue(PixelLength(map->Camera->X)));
        (void)Ui.SetValue(screen, kCameraTop, UiValue(PixelLength(map->Camera->Y)));
    }

    std::vector<UiRow> zones;
    for (const WorldMapZone& zone : map->Zones)
    {
        UiRow row{ zone.Name, StateName(zone.State), StateText(zone.State) };
        row.Selected = hot.has_value() && *hot == zone.Id;
        row.Parts = { PixelLength(zone.Plate.Min().X), PixelLength(zone.Plate.Min().Y), PixelLength(zone.Plate.Width()), PixelLength(zone.Plate.Height()),
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
        row.Parts = { PixelLength(link.From.X), PixelLength(link.From.Y), PixelLength(along.Magnitude()),
                      std::format("rotate({:.2f}deg)", angle) };
        links.push_back(row);
        if (link.AToB != link.BToA)
        {
            // The arrow two thirds along, pointing the way the link goes.
            const bool forward = link.AToB;
            const Vec2d at = link.From + along * (forward ? 0.66f : 0.34f);
            UiRow arrow{ "", "oneway", row.Detail };
            arrow.Parts = { PixelLength(at.X), PixelLength(at.Y), "0px", std::format("rotate({:.2f}deg)", forward ? angle : angle + 180.0f) };
            arrows.push_back(std::move(arrow));
        }
    }
    (void)Ui.SetRows(screen, kZones, zones);
    (void)Ui.SetRows(screen, kLinks, links);
    (void)Ui.SetRows(screen, kArrows, arrows);
}
