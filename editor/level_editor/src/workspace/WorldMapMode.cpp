#include "workspace/WorldMapMode.h"

#include "actions/ActionOffer.h"
#include "document/ZoneGraph.h"
#include "problems/ProblemList.h"
#include "ui/EditorUiStyle.h"
#include "viewport/CameraFraming.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/LevelLocation.h"
#include "workspace/PartitionOutline.h"

#include <SDL3/SDL_keycode.h>

#include <string>
#include <variant>
#include <vector>

WorldMapMode::WorldMapMode(EditorWorkspace& workspace, PartitionOutline& partition, DocumentRef level)
    : Workspace(workspace)
    , Partition(partition)
    , Level(level)
{
}

void WorldMapMode::Offer(ActionCatalog& catalog, ActionContext& context)
{
    OfferAction(catalog, context, { .Id = std::string(kAction), .Label = "World Map", .Category = "View" },
                { .Invoke = [this] { Showing = Showing == LevelContent::Map ? LevelContent::Views : LevelContent::Map; },
                  .Enabled = [this] { return Workspace.World.IsWorld(); },
                  .Checked = [this] { return Showing == LevelContent::Map; } });
}

const WorldMap* WorldMapMode::LayOut(const Rect2d& content, bool visible)
{
    if (Showing == LevelContent::Map && !Workspace.World.IsWorld())
        Showing = LevelContent::Views;
    if (Showing != LevelContent::Map || !visible)
        return nullptr;
    // The plates stay clear of the breadcrumb and view instrument above and
    // the stations below.
    const float scale = EditorUi::UiScale;
    const Rect2d area = Rect2d::FromMinMax(content.Min() + Vec2d(24.0f, 48.0f) * scale,
                                           content.Max() - Vec2d(24.0f, 96.0f) * scale);
    std::vector<ZoneId> problems;
    for (const Problem& problem : Partition.Problems(Level))
        if (const std::optional<LevelLocation> location = ParseLevelLocation(problem.Location))
            if (const ZoneId* zone = std::get_if<ZoneId>(&*location))
                problems.push_back(*zone);
    const EditorViewport* view = Workspace.Layout.Active();
    const std::vector<ZoneGraphEdge> edges = ZoneGraphEdges(Workspace.World);
    Map = LayOutWorldMap(Workspace.World, edges,
                         { .Area = area,
                           .Margin = 0.0f,
                           .MinimumPlate = 18.0f * scale,
                           .Camera = view != nullptr ? std::optional<Vec3d>(view->Camera.Position) : std::nullopt,
                           .Problems = problems });
    return &Map;
}

std::string WorldMapMode::Title() const
{
    if (!Workspace.World.IsWorld() || Workspace.World.Manifest().Name.empty())
        return "World";
    return Workspace.World.Manifest().Name;
}

InputConsumed WorldMapMode::OnInput(const InputEvent& event)
{
    if (Showing != LevelContent::Map)
        return InputConsumed::No;
    if (const auto* move = std::get_if<PointerMoveEvent>(&event))
    {
        Pointed = ZoneAt(Map, move->Position);
        return InputConsumed::Yes;
    }
    if (const auto* down = std::get_if<PointerDownEvent>(&event))
    {
        if (down->Button == MouseButton::Left)
            if (const std::optional<ZoneId> zone = ZoneAt(Map, down->Position))
                Travel(*zone);
        return InputConsumed::Yes;
    }
    if (const auto* key = std::get_if<KeyDownEvent>(&event); key != nullptr && key->Key == SDLK_ESCAPE)
    {
        Showing = LevelContent::Views;
        return InputConsumed::Yes;
    }
    return std::holds_alternative<PointerUpEvent>(event) || std::holds_alternative<WheelEvent>(event)
        ? InputConsumed::Yes
        : InputConsumed::No;
}

void WorldMapMode::Travel(ZoneId zone)
{
    WorldDocument& world = Workspace.World;
    if (zone != world.FocusZone() && !world.SetFocusZone(zone))
        return;
    for (const ZoneHeader& header : world.Manifest().Zones)
        if (header.Id == zone && header.Bounds.IsValid())
            if (EditorViewport* view = Workspace.Layout.Active())
                FrameBounds(view->Camera, header.Bounds);
    Showing = LevelContent::Views;
    Pointed.reset();
}
