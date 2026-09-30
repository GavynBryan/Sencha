#include "workspace/PartitionOutline.h"

#include "document/EditorEntityRecipe.h"
#include "document/ZoneBounds.h"
#include "document/commands/CreateSnapshotEntityCommand.h"
#include "document/commands/EditWorldManifestCommand.h"
#include "document/commands/SetZoneBoundsCommand.h"
#include "properties/SettingProperties.h"
#include "selection/commands/SelectCommand.h"
#include "text/TextFilterMatch.h"
#include "viewport/WorldViewSettings.h"
#include "workspace/EditorWorkspace.h"

#include <zone/ZoneDemand.h>

#include <algorithm>
#include <array>
#include <format>

namespace
{
constexpr std::array<std::string_view, 3> kShapes = { "Inherited", "Proximity", "Graph" };

const ZoneHeader* FindZone(const WorldPartitionManifest& manifest, ZoneId zone)
{
    const auto found = std::ranges::find(manifest.Zones, zone, &ZoneHeader::Id);
    return found != manifest.Zones.end() ? &*found : nullptr;
}

const GraphRecord* FindGraph(const WorldPartitionManifest& manifest, GraphId graph)
{
    const auto found = std::ranges::find(manifest.Graphs, graph, &GraphRecord::Id);
    return found != manifest.Graphs.end() ? &*found : nullptr;
}

std::string ZoneName(const WorldPartitionManifest& manifest, ZoneId zone)
{
    const ZoneHeader* header = FindZone(manifest, zone);
    return header == nullptr ? "<missing " + ZoneIdToString(zone) + ">"
         : header->Name.empty() ? ZoneIdToString(zone)
                                : header->Name;
}

PropertyNode Note(std::string key, std::string label, std::string text)
{
    PropertyNode node;
    node.Key = std::move(key);
    node.Descriptor.Kind = PropertyKind::Text;
    node.Descriptor.Label = std::move(label);
    node.Descriptor.ReadOnly = true;
    node.Value = std::move(text);
    return node;
}

PropertyNode Named(std::string key, std::string label, std::string value)
{
    PropertyNode node;
    node.Key = std::move(key);
    node.Descriptor.Kind = PropertyKind::Text;
    node.Descriptor.Label = std::move(label);
    node.Value = std::move(value);
    return node;
}

PropertyNode Group(std::string key, std::string label)
{
    PropertyNode node;
    node.Key = std::move(key);
    node.Descriptor.Kind = PropertyKind::Group;
    node.Descriptor.Label = std::move(label);
    return node;
}

// A starter radius for a graph turned to Proximity: the largest horizontal
// half-extent among its zones, so the first preview reaches the neighbours'
// near faces. 100 when no zone has measurable bounds yet.
double SeedGraphRadius(const WorldPartitionManifest& manifest, GraphId graph)
{
    double largest = 0.0;
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (zone.Graph != graph || !zone.Bounds.IsValid())
            continue;
        const Vec3d extent = zone.Bounds.Extent();
        largest = std::max({ largest, static_cast<double>(extent[0]), static_cast<double>(extent[2]) });
    }
    return largest > 0.0 ? largest : 100.0;
}

// A number that may be left to inherit: shown resolved, cleared by emptying it.
PropertyNode Inheritable(std::string key, std::string label, std::string resolved, bool authored,
                         std::string tooltip)
{
    PropertyNode node = Named(std::move(key), std::move(label), std::move(resolved));
    node.Descriptor.Tooltip = std::move(tooltip) + (authored ? "" : " (inherited; empty it to inherit again)");
    return node;
}
} // namespace

WorkspaceExplorerInfo PartitionOutline::Info()
{
    return { .Id = std::string(kId), .Title = "Zones", .Shape = ExplorerShape::Outline, .Action = "explorer.zones" };
}

PartitionOutline::Picked PartitionOutline::What() const
{
    if (!Workspace.World.IsWorld())
        return Picked::None;
    if (Pick == Picked::Zone && !Workspace.World.SelectedZone().IsValid())
        return Picked::None;
    if (Pick == Picked::Graph && FindGraph(Workspace.World.Manifest(), PickedGraph) == nullptr)
        return Picked::None;
    return Pick;
}

ExplorerPage PartitionOutline::Explore(const ExplorerQuery& query) const
{
    ExplorerPage page;
    WorldDocument& world = Workspace.World;
    if (!world.IsWorld())
    {
        page.Empty = "This level is not a world: it has no zones.";
        return page;
    }
    page.Actions = { "world.add_graph" };
    page.Empty = "No zone's name holds that.";
    const WorldPartitionManifest& manifest = world.Manifest();
    const Picked picked = What();

    std::vector<ExplorerItem> rows;
    const std::string worldName = manifest.Name.empty() ? std::string("World") : manifest.Name;
    rows.push_back({ .Key = "world",
                     .Label = worldName + (world.IsWorldSceneFocused() ? "  (focus)" : ""),
                     .Current = picked == Picked::World });
    for (const GraphRecord& graph : manifest.Graphs)
    {
        std::vector<ExplorerItem> zones;
        for (const ZoneHeader& zone : manifest.Zones)
        {
            if (zone.Graph != graph.Id || !TextFilterMatch(zone.Name, query.Filter))
                continue;
            std::string label = ZoneName(manifest, zone.Id);
            if (zone.Id == world.FocusZone())
                label += "  (focus)";
            else if (world.IsZoneOpen(zone.Id))
                label += "  (context)";
            if (const EditorDocument* document = world.IsZoneOpen(zone.Id) ? world.ZoneDocument(zone.Id) : nullptr;
                document != nullptr && document->IsDirty())
                label += "  *";
            bool visible = true;
            world.VisitOpenZones([&](ZoneId id, EditorDocument&, const ZoneViewState& view) {
                if (id == zone.Id)
                    visible = view.VisibleInEditor;
            });
            zones.push_back({ .Key = "zone:" + ZoneIdToString(zone.Id),
                              .Label = std::move(label),
                              .Depth = 1,
                              .Current = picked == Picked::Zone && world.SelectedZone() == zone.Id,
                              .Marks = { visible ? IconId::Eye : IconId::EyeOff } });
        }
        if (!query.Filter.empty() && zones.empty() && !TextFilterMatch(graph.Name, query.Filter))
            continue;
        rows.push_back({ .Key = "graph:" + GraphIdToString(graph.Id),
                         .Label = graph.Name.empty() ? GraphIdToString(graph.Id) : graph.Name,
                         .Branch = zones.empty() ? ExplorerBranch::Leaf : ExplorerBranch::Open,
                         .Current = picked == Picked::Graph && PickedGraph == graph.Id });
        rows.insert(rows.end(), std::make_move_iterator(zones.begin()), std::make_move_iterator(zones.end()));
    }
    page.Total = rows.size();
    for (std::size_t i = query.First; i < rows.size() && page.Items.size() < query.Count; ++i)
        page.Items.push_back(std::move(rows[i]));
    return page;
}

void PartitionOutline::Choose(std::string_view key, ExplorerChoice choice, std::size_t)
{
    WorldDocument& world = Workspace.World;
    if (!world.IsWorld())
        return;
    if (key == "world")
    {
        Pick = Picked::World;
        if (choice == ExplorerChoice::Commit)
            (void)world.FocusWorldScene();
        return;
    }
    if (key.starts_with("graph:"))
    {
        if (const std::optional<GraphId> graph = GraphIdFromString(key.substr(6)))
        {
            Pick = Picked::Graph;
            PickedGraph = *graph;
        }
        return;
    }
    if (!key.starts_with("zone:"))
        return;
    const std::optional<ZoneId> zone = ZoneIdFromString(key.substr(5));
    if (!zone.has_value())
        return;
    if (choice == ExplorerChoice::Mark)
    {
        bool visible = true;
        world.VisitOpenZones([&](ZoneId id, EditorDocument&, const ZoneViewState& view) {
            if (id == *zone)
                visible = view.VisibleInEditor;
        });
        (void)world.SetZoneVisible(*zone, !visible);
        return;
    }
    // A zone is selected in place of the entities, as one selection is shared.
    Workspace.Selection.ClearSelection();
    (void)world.SelectZone(*zone);
    Pick = Picked::Zone;
    if (choice == ExplorerChoice::Commit)
        (void)world.SetFocusZone(*zone);
}

std::vector<Problem> PartitionOutline::Problems(const DocumentRef& document) const
{
    std::vector<Problem> problems;
    if (!Workspace.World.IsWorld())
        return problems;
    for (const ContentRiskRecord& record : Workspace.World.ValidationRecords())
    {
        std::string location;
        switch (record.Kind)
        {
        case ContentRiskSourceKind::Zone:       location = "zone:" + ZoneIdToString(ZoneId{ record.SourceId }); break;
        case ContentRiskSourceKind::Graph:      location = "graph:" + GraphIdToString(GraphId{ record.SourceId }); break;
        case ContentRiskSourceKind::Transition:
            location = "transition:" + TransitionIdToString(TransitionId{ record.SourceId });
            break;
        default:                                location = "world"; break;
        }
        problems.push_back({ .Severity = record.Severity == ContentRiskSeverity::Error ? ProblemSeverity::Error
                                                                                       : ProblemSeverity::Warning,
                             .Message = record.Message,
                             .Location = std::move(location),
                             .Document = document });
    }
    return problems;
}

bool PartitionOutline::Reveal(std::string_view location)
{
    WorldDocument& world = Workspace.World;
    if (!world.IsWorld())
        return false;
    ZoneId zone{};
    if (location.starts_with("graph:"))
    {
        const std::optional<GraphId> graph = GraphIdFromString(location.substr(6));
        if (!graph.has_value())
            return false;
        Pick = Picked::Graph;
        PickedGraph = *graph;
        return true;
    }
    if (location.starts_with("zone:"))
        zone = ZoneIdFromString(location.substr(5)).value_or(ZoneId{});
    else if (location.starts_with("transition:"))
        if (const std::optional<TransitionId> transition = TransitionIdFromString(location.substr(11)))
            for (const TransitionRecord& record : world.Manifest().Transitions)
                if (record.Id == *transition)
                    zone = record.From;
    if (location == "world")
    {
        Pick = Picked::World;
        return true;
    }
    if (!zone.IsValid())
        return false;
    Workspace.Selection.ClearSelection();
    (void)world.SelectZone(zone);
    Pick = Picked::Zone;
    return true;
}

std::string PartitionOutline::Title() const
{
    const WorldDocument& world = Workspace.World;
    switch (What())
    {
    case Picked::World:
        return Workspace.World.Manifest().Name.empty() ? "World" : Workspace.World.Manifest().Name;
    case Picked::Graph:
        return FindGraph(Workspace.World.Manifest(), PickedGraph)->Name;
    case Picked::Zone:
        return ZoneName(Workspace.World.Manifest(), world.SelectedZone());
    case Picked::None:
    default:
        return {};
    }
}

std::vector<PropertyNode> PartitionOutline::Nodes() const
{
    switch (What())
    {
    case Picked::World: return WorldNodes();
    case Picked::Graph: return GraphNodes(PickedGraph);
    case Picked::Zone:  return ZoneNodes(Workspace.World.SelectedZone());
    case Picked::None:
    default:            return {};
    }
}

std::vector<PropertyNode> PartitionOutline::ZoneNodes(ZoneId id) const
{
    WorldDocument& world = Workspace.World;
    const WorldPartitionManifest& manifest = world.Manifest();
    const ZoneHeader* zone = FindZone(manifest, id);
    if (zone == nullptr)
        return {};
    const bool focus = world.FocusZone() == id;
    const bool open = world.IsZoneOpen(id);
    const EditorDocument* document = open ? world.ZoneDocument(id) : nullptr;
    const bool dirty = document != nullptr && document->IsDirty();

    PropertyNode group = Group("zone", "Zone");
    if (!focus)
        group.Descriptor.Verbs.push_back({ .Id = "focus", .Label = "Focus" });
    if (!open)
        group.Descriptor.Verbs.push_back({ .Id = "load", .Label = "Load as Context" });
    if (open && !focus && !dirty)
        group.Descriptor.Verbs.push_back({ .Id = "unload", .Label = "Unload" });
    if (zone->BoundsOverridden && open)
        group.Descriptor.Verbs.push_back({ .Id = "bounds", .Label = "Bounds From Contents" });
    std::vector<std::string> others;
    for (const ZoneHeader& other : manifest.Zones)
        if (other.Id != id)
            others.push_back(ZoneName(manifest, other.Id));
    if (!others.empty())
        group.Descriptor.Verbs.push_back({ .Id = "link", .Label = "Teleport Link To", .Choices = std::move(others) });

    group.Children.push_back(Named("name", "Name", zone->Name));
    PropertyNode graph;
    graph.Key = "graph";
    graph.Descriptor.Kind = PropertyKind::Choice;
    graph.Descriptor.Label = "Graph";
    for (const GraphRecord& record : manifest.Graphs)
        graph.Descriptor.Choices.push_back(record.Name);
    if (const GraphRecord* record = FindGraph(manifest, zone->Graph))
        graph.Value = record->Name;
    group.Children.push_back(std::move(graph));
    group.Children.push_back(Note("state", "State", focus ? "Focus" : open ? "Loaded as context" : "Not loaded"));
    const Vec3d min = zone->Bounds.Min;
    const Vec3d max = zone->Bounds.Max;
    group.Children.push_back(Note("bounds", zone->BoundsOverridden ? "Bounds (set)" : "Bounds",
                                  std::format("{:g}, {:g}, {:g} to {:g}, {:g}, {:g}", min.X, min.Y, min.Z, max.X,
                                              max.Y, max.Z)));
    return { std::move(group) };
}

std::vector<PropertyNode> PartitionOutline::GraphNodes(GraphId id) const
{
    const WorldPartitionManifest& manifest = Workspace.World.Manifest();
    const GraphRecord* graph = FindGraph(manifest, id);
    if (graph == nullptr)
        return {};
    // What is in force for a zone of this graph, whether authored or not.
    ZoneId sample{};
    for (const ZoneHeader& zone : manifest.Zones)
        if (zone.Graph == id)
            sample = zone.Id;
    const WorldPartitionStreamingConfig resolved =
        sample.IsValid() ? ResolveGraphStreamingConfig(manifest, sample, WorldPartitionStreamingConfig{})
                         : WorldPartitionStreamingConfig{};
    const GraphStreamingConfig& authored = graph->Streaming;

    PropertyNode group = Group("graph", "Graph");
    group.Descriptor.Verbs.push_back({ .Id = "add_zone", .Label = "Add Zone" });
    group.Children.push_back(Named("name", "Name", graph->Name));
    const std::size_t shape = !authored.Radius.has_value() ? 0 : *authored.Radius > 0.0 ? 1 : 2;
    group.Children.push_back(ChoiceProperty("shape", "Streaming", kShapes, shape,
                                            "Proximity loads zones by distance; Graph through authored connections"));
    group.Children.push_back(Inheritable("radius", "Radius", std::format("{:g}", resolved.Radius),
                                         authored.Radius.has_value(), "How far zones load around the focus"));
    group.Children.push_back(Inheritable("hops", "Hops", std::to_string(resolved.HopCount),
                                         authored.HopCount.has_value(), "How many connections out zones preload"));
    group.Children.push_back(Inheritable("cap", "Resident cap", std::to_string(resolved.ResidentZoneCap),
                                         authored.ResidentZoneCap.has_value(), "Most zones resident at once"));
    return { std::move(group) };
}

std::vector<PropertyNode> PartitionOutline::WorldNodes() const
{
    WorldDocument& world = Workspace.World;
    const WorldPartitionManifest& manifest = world.Manifest();
    PropertyNode group = Group("world", "World");
    group.Descriptor.Verbs.push_back({ .Id = "add_graph", .Label = "Add Graph" });
    if (!world.IsWorldSceneFocused())
        group.Descriptor.Verbs.push_back({ .Id = "focus", .Label = "Focus World Scene" });
    if (world.FocusZone().IsValid())
        group.Descriptor.Verbs.push_back({ .Id = "add_dock", .Label = "Add Dock in Focus Zone" });
    group.Children.push_back(Note("zones", "Zones", std::to_string(manifest.Zones.size())));

    // Old connections block new-format saves until they are replaced.
    if (!manifest.Transitions.empty())
    {
        PropertyNode legacy = Group("legacy", "Legacy transitions");
        legacy.Descriptor.Tooltip = "Old connections block new-format saves. Teleport rows become links; a doorway "
                                    "or seam row needs a Dock placed at the crossing, then discarding.";
        if (std::ranges::any_of(manifest.Transitions, [](const TransitionRecord& transition) {
                return transition.Topology == TransitionTopology::Teleport;
            }))
            legacy.Descriptor.Verbs.push_back({ .Id = "migrate", .Label = "Migrate Teleport Rows" });
        for (const TransitionRecord& transition : manifest.Transitions)
        {
            PropertyNode row = Note("transition:" + TransitionIdToString(transition.Id),
                                    ZoneName(manifest, transition.From) + " to " + ZoneName(manifest, transition.To),
                                    transition.Name);
            row.Descriptor.Verbs = { { .Id = "convert", .Label = "Convert to Link" },
                                     { .Id = "discard", .Label = "Discard" } };
            legacy.Children.push_back(std::move(row));
        }
        group.Children.push_back(std::move(legacy));
    }

    // What streaming would load around the focus, with what-if overrides that
    // edit nothing.
    if (WorldViewSettings* view = world.ViewSettings())
    {
        PropertyNode preview = Group("preview", "Streaming preview");
        preview.Children.push_back(ToggleProperty("preview.on", "Show", view->StreamingPreview));
        const ZoneId focus = view->PreviewFocus.IsValid() ? view->PreviewFocus : world.FocusZone();
        if (view->StreamingPreview && focus.IsValid())
        {
            const WorldPartitionStreamingConfig resolved =
                ResolveGraphStreamingConfig(manifest, focus, WorldPartitionStreamingConfig{});
            preview.Children.push_back(Inheritable("preview.hops", "Hops",
                                                   std::to_string(view->PreviewHopCount.value_or(resolved.HopCount)),
                                                   view->PreviewHopCount.has_value(), "What-if: edits nothing"));
            preview.Children.push_back(Inheritable(
                "preview.radius", "Radius", std::format("{:g}", view->PreviewRadius.value_or(static_cast<float>(resolved.Radius))),
                view->PreviewRadius.has_value(), "What-if: edits nothing"));
            preview.Children.push_back(Inheritable(
                "preview.cap", "Cap", std::to_string(view->PreviewResidentCap.value_or(resolved.ResidentZoneCap)),
                view->PreviewResidentCap.has_value(), "What-if: edits nothing"));
            const std::vector<ZoneDemandRecord> demand =
                ComputeZoneDemand(manifest, world.Index(), focus, {},
                                  ResolvePreviewStreamingConfig(manifest, focus, *view), &view->PreviewFocusPosition);
            for (const ZoneDemandRecord& record : demand)
            {
                std::string why = DescribeZoneDemandReasons(record);
                why += IsDemandedFor(record, ZoneDemandReason::Focus) ? ", live"
                     : record.Desired.Visible                        ? ", render preload"
                                                                     : ", dormant preload";
                preview.Children.push_back(
                    Note("demand:" + ZoneIdToString(record.Zone), ZoneName(manifest, record.Zone), std::move(why)));
            }
        }
        group.Children.push_back(std::move(preview));
    }
    return { std::move(group) };
}

std::string PartitionOutline::ValueOf(std::string_view key) const
{
    const auto find = [&](auto&& self, const std::vector<PropertyNode>& nodes) -> std::string {
        for (const PropertyNode& node : nodes)
        {
            if (node.Key == key)
                return node.Value;
            if (std::string found = self(self, node.Children); !found.empty())
                return found;
        }
        return {};
    };
    return find(find, Nodes());
}

template <typename Verb>
bool PartitionOutline::Edit(Verb&& verb)
{
    WorldPartitionManifest before = Workspace.World.Manifest();
    if (!verb())
        return false;
    RecordManifestEdit(Workspace.World, Workspace.Commands, std::move(before));
    return true;
}

bool PartitionOutline::BeginEdit(std::string_view)
{
    return What() != Picked::None;
}

PropertyEditResult PartitionOutline::PreviewEdit(std::string_view, std::string_view)
{
    return PropertyEditResult::Unchanged;
}

void PartitionOutline::CancelEdit(std::string_view)
{
}

PropertyEditResult PartitionOutline::CommitEdit(std::string_view key, std::string_view value)
{
    WorldDocument& world = Workspace.World;
    if (value == ValueOf(key))
        return PropertyEditResult::Unchanged;
    const auto applied = [](bool changed) { return changed ? PropertyEditResult::Applied : PropertyEditResult::Refused; };
    const auto count = [&](std::string_view text, int least) -> std::optional<std::optional<int32_t>> {
        if (text.empty())
            return std::optional<int32_t>{};
        const std::optional<double> number = ParseNumber(text);
        if (!number.has_value())
            return std::nullopt;
        return std::optional<int32_t>{ std::max(least, static_cast<int32_t>(std::lround(*number))) };
    };

    switch (What())
    {
    case Picked::Zone:
    {
        const ZoneId zone = world.SelectedZone();
        if (key == "name")
            return applied(Edit([&] { return world.RenameZone(zone, std::string(value)); }));
        if (key == "graph")
            for (const GraphRecord& graph : world.Manifest().Graphs)
                if (graph.Name == value)
                    return applied(Edit([&, id = graph.Id] { return world.SetZoneGraph(zone, id); }));
        return PropertyEditResult::Refused;
    }
    case Picked::Graph:
    {
        const GraphId graph = PickedGraph;
        if (key == "name")
            return applied(Edit([&] { return world.RenameGraph(graph, std::string(value)); }));
        if (key == "shape")
        {
            const std::optional<std::size_t> shape = ParseChoice(value, kShapes);
            if (!shape.has_value())
                return PropertyEditResult::Refused;
            if (*shape == 0)
                return applied(Edit([&] { return world.SetGraphRadius(graph, std::nullopt); }));
            if (*shape == 2)
                return applied(Edit([&] { return world.SetGraphRadius(graph, 0.0); }));
            return applied(Edit([&] { return world.SetGraphRadius(graph, SeedGraphRadius(world.Manifest(), graph)); }));
        }
        if (key == "radius")
        {
            if (value.empty())
                return applied(Edit([&] { return world.SetGraphRadius(graph, std::nullopt); }));
            const std::optional<double> radius = ParseNumber(value, 0.0);
            return radius.has_value() ? applied(Edit([&] { return world.SetGraphRadius(graph, *radius); }))
                                      : PropertyEditResult::Refused;
        }
        if (key == "hops" || key == "cap")
        {
            const std::optional<std::optional<int32_t>> parsed = count(value, key == "hops" ? 0 : 1);
            if (!parsed.has_value())
                return PropertyEditResult::Refused;
            return applied(Edit([&] {
                return key == "hops" ? world.SetGraphHopCount(graph, *parsed) : world.SetGraphResidentCap(graph, *parsed);
            }));
        }
        return PropertyEditResult::Gone;
    }
    case Picked::World:
    {
        WorldViewSettings* view = world.ViewSettings();
        if (view == nullptr)
            return PropertyEditResult::Gone;
        if (key == "preview.on")
        {
            const std::optional<bool> on = ParseToggle(value);
            if (!on.has_value())
                return PropertyEditResult::Refused;
            view->StreamingPreview = *on;
            return PropertyEditResult::Applied;
        }
        if (key == "preview.radius")
        {
            if (value.empty())
                view->PreviewRadius.reset();
            else if (const std::optional<double> radius = ParseNumber(value, 0.0))
                view->PreviewRadius = static_cast<float>(*radius);
            else
                return PropertyEditResult::Refused;
            return PropertyEditResult::Applied;
        }
        if (key == "preview.hops" || key == "preview.cap")
        {
            const std::optional<std::optional<int32_t>> parsed = count(value, key == "preview.hops" ? 0 : 1);
            if (!parsed.has_value())
                return PropertyEditResult::Refused;
            (key == "preview.hops" ? view->PreviewHopCount : view->PreviewResidentCap) =
                parsed->has_value() ? std::optional<int>(**parsed) : std::nullopt;
            return PropertyEditResult::Applied;
        }
        return PropertyEditResult::Gone;
    }
    case Picked::None:
    default:
        return PropertyEditResult::Gone;
    }
}

PropertyEditResult PartitionOutline::Act(std::string_view key, std::string_view verb, std::string_view choice)
{
    WorldDocument& world = Workspace.World;
    if (!world.IsWorld())
        return PropertyEditResult::Gone;
    const auto applied = [](bool changed) { return changed ? PropertyEditResult::Applied : PropertyEditResult::Refused; };
    const WorldPartitionManifest& manifest = world.Manifest();

    if (key == "zone" && What() == Picked::Zone)
    {
        const ZoneId zone = world.SelectedZone();
        const ZoneHeader* header = FindZone(manifest, zone);
        if (header == nullptr)
            return PropertyEditResult::Gone;
        if (verb == "focus")
            return applied(world.SetFocusZone(zone));
        if (verb == "load")
            return applied(world.LoadZone(zone));
        if (verb == "unload")
            return applied(world.UnloadZone(zone));
        if (verb == "bounds")
        {
            Aabb3d derived = header->Bounds;
            if (EditorDocument* document = world.ZoneDocument(zone))
                if (const std::optional<Aabb3d> bounds = ComputeZoneBounds(document->GetScene()))
                    derived = *bounds;
            Workspace.Commands.Execute(
                std::make_unique<SetZoneBoundsCommand>(world, zone, header->Bounds, true, derived, false));
            return PropertyEditResult::Applied;
        }
        if (verb == "link")
        {
            const auto destination = std::ranges::find_if(manifest.Zones, [&](const ZoneHeader& other) {
                return other.Id != zone && ZoneName(manifest, other.Id) == choice;
            });
            if (destination == manifest.Zones.end())
                return PropertyEditResult::Refused;
            EditorCreateContext context{ .World = &world, .ActiveZone = zone, .ActiveGraph = header->Graph,
                                         .Selection = Workspace.Selection.GetSelection() };
            WorldLinkRecipe recipe(destination->Id, DockDirectionBoth);
            EntitySnapshot snapshot = recipe.Build(context);
            if (!snapshot.Components.IsObject())
                return PropertyEditResult::Refused;
            // Focus swaps the active document and clears its stack: first, so
            // the creation is what undo takes back.
            (void)world.FocusWorldScene();
            Workspace.Commands.Execute(
                std::make_unique<CreateSnapshotEntityCommand>(world.WorldSceneDocument(), std::move(snapshot)));
            return PropertyEditResult::Applied;
        }
        return PropertyEditResult::Gone;
    }
    if (key == "graph" && What() == Picked::Graph && verb == "add_zone")
        return applied(Edit([&] { return world.AddZone(PickedGraph, "New Zone").IsValid(); }));
    if (key == "world" && What() == Picked::World)
    {
        if (verb == "add_graph")
            return applied(Edit([&] { return world.AddGraph("New Graph").IsValid(); }));
        if (verb == "focus")
            return applied(world.FocusWorldScene());
        if (verb == "add_dock")
        {
            const ZoneHeader* zone = FindZone(manifest, world.FocusZone());
            const IEditorEntityRecipe* recipe = Workspace.CreationRecipes.Find("world_dock");
            if (zone == nullptr || recipe == nullptr)
                return PropertyEditResult::Refused;
            const Vec3d center = zone->Bounds.Center();
            EditorCreateContext context{ .World = &world, .ActiveZone = zone->Id, .ActiveGraph = zone->Graph,
                                         .PlacementPoint = { center.X, center.Y, zone->Bounds.Min.Z },
                                         .PlacementNormal = Vec3d::Forward(),
                                         .Selection = Workspace.Selection.GetSelection() };
            EntitySnapshot snapshot = recipe->Build(context);
            if (!snapshot.Components.IsObject())
                return PropertyEditResult::Refused;
            (void)world.FocusWorldScene();
            auto create = std::make_unique<CreateSnapshotEntityCommand>(world.WorldSceneDocument(), std::move(snapshot));
            CreateSnapshotEntityCommand* created = create.get();
            Workspace.Commands.Execute(std::move(create));
            Workspace.Commands.Execute(std::make_unique<SelectCommand>(
                Workspace.Selection, SelectableRef::EntitySelection(world.WorldSceneDocument().GetRegistry().Id,
                                                                    created->CreatedEntity())));
            return PropertyEditResult::Applied;
        }
        return PropertyEditResult::Gone;
    }
    if (key == "legacy" && verb == "migrate")
        return applied(!world.MigrateLegacyTransitions().Links.empty());
    if (key.starts_with("transition:"))
        if (const std::optional<TransitionId> transition = TransitionIdFromString(key.substr(11)))
        {
            if (verb == "convert")
                return applied(world.ConvertLegacyTransitionToTeleport(*transition));
            if (verb == "discard")
                return applied(world.DiscardLegacyTransition(*transition));
        }
    return PropertyEditResult::Gone;
}
