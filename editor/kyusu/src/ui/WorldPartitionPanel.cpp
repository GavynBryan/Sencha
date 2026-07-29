#include "WorldPartitionPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "fonts/IconsFontAwesome6.h"

#include "commands/CommandStack.h"
#include "document/EditorEntityRecipe.h"
#include "document/WorldDocument.h"
#include "document/ZoneBounds.h"
#include "document/commands/CreateSnapshotEntityCommand.h"
#include "document/commands/SetZoneBoundsCommand.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"
#include "selection/SelectionService.h"
#include "selection/commands/SelectCommand.h"
#include "viewport/WorldViewSettings.h"

#include <core/logging/LoggingProvider.h>
#include <zone/ZoneDemand.h>

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

WorldPartitionPanel::WorldPartitionPanel(WorldDocument& world, SelectionService& selection,
                                         CommandStack& commands,
                                         EditorEntityRecipeRegistry& recipes)
    : WorldDoc(world)
    , Selection(selection)
    , Commands(commands)
    , Recipes(recipes)
{
}

std::string_view WorldPartitionPanel::GetTitle() const
{
    return "World";
}

void WorldPartitionPanel::OnDraw()
{
    if (!WorldDoc.IsWorld())
        return;

    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    DrawHeaderButtons();
    DrawLegacyTransitionMigration();
    ImGui::Separator();

    DrawWorldSceneRow();

    for (const GraphRecord& graph : WorldDoc.Manifest().Graphs)
        DrawGraph(graph);

    // Zones whose graph reference resolves to no graph record still need a
    // home in the tree, or they would be unreachable for repair.
    bool orphanHeader = false;
    for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
    {
        const bool known = [&]
        {
            for (const GraphRecord& graph : WorldDoc.Manifest().Graphs)
                if (graph.Id == zone.Graph)
                    return true;
            return false;
        }();
        if (known)
            continue;
        if (!orphanHeader)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
            ImGui::TextUnformatted("Unassigned");
            ImGui::PopStyleColor();
            orphanHeader = true;
        }
        DrawZoneRow(zone);
    }

    DrawStreamingPreview();

    ImGui::Separator();
    DrawValidation();
}

void WorldPartitionPanel::DrawStreamingPreview()
{
    WorldViewSettings* view = WorldDoc.ViewSettings();
    if (view == nullptr)
        return;

    ImGui::Separator();
    ImGui::Checkbox("Streaming Preview", &view->StreamingPreview);
    if (!view->StreamingPreview)
        return;

    ZoneId previewFocus = view->PreviewFocus;
    if (!previewFocus.IsValid())
        previewFocus = WorldDoc.FocusZone();
    if (!previewFocus.IsValid())
    {
        ImGui::TextDisabled("No zone to preview around yet");
        return;
    }

    // The config in force: focus selects the graph whose shape is active.
    // This line is what makes that rule legible at graph boundaries.
    const GraphRecord* focusGraph = nullptr;
    for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
        if (header.Id == previewFocus)
            for (const GraphRecord& graph : WorldDoc.Manifest().Graphs)
                if (graph.Id == header.Graph)
                    focusGraph = &graph;
    const WorldPartitionStreamingConfig resolved = ResolveGraphStreamingConfig(
        WorldDoc.Manifest(), previewFocus, WorldPartitionStreamingConfig{});
    {
        const GraphStreamingConfig authored =
            focusGraph != nullptr ? focusGraph->Streaming : GraphStreamingConfig{};
        std::string line = focusGraph != nullptr
            ? std::format("Shape from \"{}\":", focusGraph->Name)
            : std::string{ "Shape from world base:" };
        line += std::format(" Hop {}{}", resolved.HopCount,
                            authored.HopCount ? "" : " (inherited)");
        line += std::format(", Radius {:g}{}", resolved.Radius,
                            authored.Radius ? "" : " (inherited)");
        line += std::format(", Cap {}{}", resolved.ResidentZoneCap,
                            authored.ResidentZoneCap ? "" : " (inherited)");
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
        ImGui::TextWrapped("%s", line.c_str());
        ImGui::PopStyleColor();
    }

    // Per-field preview overrides: absent shows the resolved value, edited
    // overrides that one field for the preview only.
    const auto clearButton = [](auto& field, const char* id)
    {
        if (!field.has_value())
            return;
        ImGui::SameLine();
        ImGui::PushID(id);
        if (ImGui::SmallButton(ICON_FA_XMARK))
            field.reset();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Clear the preview override (back to the authored shape)");
        ImGui::PopID();
    };
    ImGui::SetNextItemWidth(80.0f);
    int hops = view->PreviewHopCount.value_or(resolved.HopCount);
    if (ImGui::InputInt("Hops", &hops))
        view->PreviewHopCount = hops < 0 ? 0 : hops;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("What-if override: preview with a different preload hop count. "
                          "Edits nothing; clear to see the authored shape.");
    clearButton(view->PreviewHopCount, "clear_hops");
    ImGui::SetNextItemWidth(80.0f);
    float radius = view->PreviewRadius.value_or(static_cast<float>(resolved.Radius));
    if (ImGui::InputFloat("Radius", &radius, 0.0f, 0.0f, "%.0f"))
        view->PreviewRadius = radius < 0.0f ? 0.0f : radius;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("What-if override: preview with a different load radius. "
                          "Edits nothing; clear to see the authored shape.");
    clearButton(view->PreviewRadius, "clear_radius");
    ImGui::SetNextItemWidth(80.0f);
    int cap = view->PreviewResidentCap.value_or(resolved.ResidentZoneCap);
    if (ImGui::InputInt("Cap", &cap))
        view->PreviewResidentCap = cap < 1 ? 1 : cap;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("What-if override: preview with a different zone cap. "
                          "Edits nothing; clear to see the authored shape.");
    clearButton(view->PreviewResidentCap, "clear_cap");

    const auto zoneName = [&](ZoneId zone) -> const char*
    {
        for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
            if (header.Id == zone)
                return header.Name.c_str();
        return "<unknown>";
    };

    const auto records = ComputeZoneDemand(
        WorldDoc.Manifest(), WorldDoc.Index(), previewFocus, {},
        ResolvePreviewStreamingConfig(WorldDoc.Manifest(), previewFocus, *view),
        &view->PreviewFocusPosition);
    for (const ZoneDemandRecord& record : records)
    {
        std::string why = DescribeZoneDemandReasons(record);
        why += IsDemandedFor(record, ZoneDemandReason::Focus) ? ", live"
             : record.Desired.Visible ? ", render preload"
                                      : ", dormant preload";
        ImGui::BulletText("%s (%s)", zoneName(record.Zone), why.c_str());
    }
    const size_t total = WorldDoc.Manifest().Zones.size();
    if (total > records.size())
        ImGui::TextDisabled("%zu of %zu zones stay unloaded", total - records.size(), total);
}

void WorldPartitionPanel::DrawWorldSceneRow()
{
    ImGui::PushID("world_scene_row");

    const bool isFocus = WorldDoc.IsWorldSceneFocused();
    if (isFocus)
    {
        ImGui::TextColored(EditorUi::Accent, "F");
        ImGui::SameLine();
    }

    std::string name{ WorldDoc.Manifest().Name };
    if (name.empty())
        name = "World";
    std::string label = ICON_FA_GLOBE "  " + name;
    if (WorldDoc.WorldSceneDocument().IsDirty())
        label += " " ICON_FA_CIRCLE_DOT;
    label += "##world_scene_row";
    ImGui::Selectable(label.c_str(), isFocus, ImGuiSelectableFlags_AllowDoubleClick);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("The world scene: entities that live for the whole world "
                          "(player start, global volumes, persistent props). Loaded once "
                          "at world start, never streamed.");
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            (void)WorldDoc.FocusWorldScene();
    }

    if (ImGui::BeginPopupContextItem("##world_scene_ctx"))
    {
        if (ImGui::MenuItem("Focus"))
            (void)WorldDoc.FocusWorldScene();
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

void WorldPartitionPanel::DrawHeaderButtons()
{
    const auto createWorldEntity = [&](EntitySnapshot snapshot)
    {
        if (!snapshot.Components.IsObject())
            return false;
        // Focus swaps the active document and clears its command stack, so do
        // it before recording creation. Undo must retain the create.
        (void)WorldDoc.FocusWorldScene();
        auto create = std::make_unique<CreateSnapshotEntityCommand>(
            WorldDoc.WorldSceneDocument(), std::move(snapshot));
        CreateSnapshotEntityCommand* raw = create.get();
        Commands.Execute(std::move(create));
        Commands.Execute(std::make_unique<SelectCommand>(
            Selection, SelectableRef::EntitySelection(
                WorldDoc.WorldSceneDocument().GetRegistry().Id,
                raw->CreatedEntity())));
        return true;
    };

    if (ImGui::Button(ICON_FA_PLUS "  Graph"))
        (void)WorldDoc.AddGraph("New Graph");

    // New zones land in the focus zone's graph (fallback: the first graph).
    GraphId activeGraph;
    for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
        if (zone.Id == WorldDoc.FocusZone())
            activeGraph = zone.Graph;
    if (!activeGraph.IsValid() && !WorldDoc.Manifest().Graphs.empty())
        activeGraph = WorldDoc.Manifest().Graphs[0].Id;
    ImGui::SameLine();
    ImGui::BeginDisabled(!activeGraph.IsValid());
    if (ImGui::Button(ICON_FA_PLUS "  Zone"))
        (void)WorldDoc.AddZone(activeGraph, "New Zone");
    ImGui::EndDisabled();

    const ZoneHeader* activeZone = nullptr;
    for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
        if (zone.Id == WorldDoc.FocusZone())
            activeZone = &zone;
    ImGui::SameLine();
    ImGui::BeginDisabled(activeZone == nullptr);
    if (ImGui::Button(ICON_FA_PLUS "  Dock") && activeZone != nullptr)
    {
        const Vec3d center = activeZone->Bounds.Center();
        EditorCreateContext context{
            .World = &WorldDoc,
            .ActiveZone = activeZone->Id,
            .ActiveGraph = activeZone->Graph,
            .PlacementPoint = { center.X, center.Y, activeZone->Bounds.Min.Z },
            .PlacementNormal = Vec3d::Forward(),
            .Selection = Selection.GetSelection(),
        };
        const IEditorEntityRecipe* recipe = Recipes.Find("world_dock");
        EntitySnapshot snapshot = recipe != nullptr ? recipe->Build(context)
                                                     : EntitySnapshot{};
        (void)createWorldEntity(std::move(snapshot));
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(activeZone == nullptr
                         || WorldDoc.Manifest().Zones.size() < 2);
    if (ImGui::Button(ICON_FA_PLUS "  Teleport Link") && activeZone != nullptr)
    {
        TeleportSource_ = activeZone->Id;
        if (TeleportDestination_ == TeleportSource_
            || !TeleportDestination_.IsValid())
        {
            TeleportDestination_ = {};
            for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
                if (zone.Id != TeleportSource_)
                {
                    TeleportDestination_ = zone.Id;
                    break;
                }
        }
        ImGui::OpenPopup("Create Teleport Link");
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("A teleport is a WorldLink between two Zones; rooms need no "
                          "eligibility flag. Focus a source Zone and ensure another "
                          "Zone exists.");

    if (ImGui::BeginPopup("Create Teleport Link"))
    {
        const ZoneHeader* source = nullptr;
        const ZoneHeader* destination = nullptr;
        for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
        {
            if (zone.Id == TeleportSource_)
                source = &zone;
            if (zone.Id == TeleportDestination_)
                destination = &zone;
        }
        ImGui::TextWrapped("Create a non-spatial graph edge. This does not add Dock "
                           "geometry and does not change either Zone's AABB.");
        ImGui::Text("Source: %s", source != nullptr
            ? source->Name.c_str() : "<missing Zone>");
        const char* destinationName = destination != nullptr
            ? destination->Name.c_str() : "<select destination>";
        if (ImGui::BeginCombo("Destination", destinationName))
        {
            for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
                if (zone.Id != TeleportSource_)
                    if (ImGui::Selectable(zone.Name.c_str(),
                                          zone.Id == TeleportDestination_))
                        TeleportDestination_ = zone.Id;
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Bidirectional", &TeleportBidirectional_);
        if (!TeleportBidirectional_)
            ImGui::TextDisabled("One way: source to destination");

        ImGui::BeginDisabled(source == nullptr || destination == nullptr
                             || source == destination);
        if (ImGui::Button("Create WorldLink"))
        {
            EditorCreateContext context{
                .World = &WorldDoc,
                .ActiveZone = TeleportSource_,
                .ActiveGraph = source != nullptr ? source->Graph : GraphId{},
                .Selection = Selection.GetSelection(),
            };
            WorldLinkRecipe recipe(
                TeleportDestination_, TeleportBidirectional_
                    ? DockDirectionBoth : DockDirectionAToB);
            if (createWorldEntity(recipe.Build(context)))
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void WorldPartitionPanel::DrawLegacyTransitionMigration()
{
    if (WorldDoc.Manifest().Transitions.empty())
    {
        if (!MigrationSummary_.empty())
            ImGui::TextColored(EditorUi::Success, "%s", MigrationSummary_.c_str());
        return;
    }

    ImGui::Separator();
    ImGui::TextColored(EditorUi::Warning, ICON_FA_TRIANGLE_EXCLAMATION
                       "  Legacy transitions block new-format saves");
    ImGui::TextWrapped(
        "These rows are old connections, not room eligibility flags. Existing "
        "Teleport rows can become WorldLinks automatically. Doorway and Seam rows "
        "have no usable plane geometry: create one Dock with + Dock, set its Zone "
        "A/B references, place its bounded plane at the crossing, then discard the "
        "legacy row(s) it replaces. If a row was actually non-spatial, explicitly "
        "convert it to a Teleport Link below.");
    const bool hasTeleport = std::any_of(
        WorldDoc.Manifest().Transitions.begin(),
        WorldDoc.Manifest().Transitions.end(),
        [](const TransitionRecord& transition)
        { return transition.Topology == TransitionTopology::Teleport; });
    ImGui::BeginDisabled(!hasTeleport);
    if (ImGui::Button("Migrate Existing Teleport Rows"))
    {
        const LegacyTransitionMigrationReport report =
            WorldDoc.MigrateLegacyTransitions();
        MigrationSummary_ = std::format(
            "Created {} links, collapsed {} reverse pairs, {} geometric records unresolved",
            report.Links.size(), report.CollapsedPairs, report.Unresolved.size());
    }
    ImGui::EndDisabled();

    const auto zoneName = [&](ZoneId id)
    {
        for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
            if (zone.Id == id)
                return zone.Name.empty() ? ZoneIdToString(id) : zone.Name;
        return std::string{ "<missing " } + ZoneIdToString(id) + ">";
    };
    TransitionId convert;
    TransitionId discard;
    for (const TransitionRecord& transition : WorldDoc.Manifest().Transitions)
    {
        ImGui::PushID(static_cast<int>(transition.Id.Value & 0x7fffffff));
        const char* kind = transition.Topology == TransitionTopology::Teleport
            ? "Teleport" : transition.Topology == TransitionTopology::Doorway
                ? "Doorway" : "Seam";
        const std::string from = zoneName(transition.From);
        const std::string to = zoneName(transition.To);
        ImGui::Text("%s: %s -> %s%s", kind, from.c_str(), to.c_str(),
                    transition.Flags.OneWay ? " (one way)" : "");
        if (transition.Topology != TransitionTopology::Teleport)
        {
            if (ImGui::SmallButton("Convert to Teleport Link"))
                convert = transition.Id;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Use only when this legacy connection is truly "
                                  "non-spatial. A reciprocal row is collapsed into "
                                  "the same bidirectional WorldLink.");
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(ICON_FA_TRASH "  Discard Replaced Row"))
            discard = transition.Id;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Discard only after a WorldDock or WorldLink replaces "
                              "this legacy connection.");
        ImGui::PopID();
    }
    if (convert.IsValid())
    {
        if (WorldDoc.ConvertLegacyTransitionToTeleport(convert))
            MigrationSummary_ = "Converted the selected legacy connection to a WorldLink";
    }
    if (discard.IsValid())
        (void)WorldDoc.DiscardLegacyTransition(discard);
}

bool WorldPartitionPanel::DrawRenameField(bool active)
{
    if (!active)
        return false;
    ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##rename", RenameBuffer_, sizeof(RenameBuffer_));
    return true;
}

void WorldPartitionPanel::DrawGraph(const GraphRecord& graph)
{
    ImGui::PushID(static_cast<int>(graph.Id.Value & 0x7fffffff));

    if (RenamingGraph_ == graph.Id)
    {
        DrawRenameField(true);
        if (ImGui::IsItemDeactivated())
        {
            if (RenameBuffer_[0] != '\0')
                (void)WorldDoc.RenameGraph(graph.Id, RenameBuffer_);
            RenamingGraph_ = GraphId{};
        }
        ImGui::PopID();
        return;
    }

    if (NavigateGraph_ == graph.Id)
    {
        ImGui::SetNextItemOpen(true);
        NavigateGraph_ = GraphId{};
    }
    const bool open = ImGui::TreeNodeEx(graph.Name.c_str(),
                                        ImGuiTreeNodeFlags_DefaultOpen
                                            | ImGuiTreeNodeFlags_SpanAvailWidth);

    if (ImGui::BeginPopupContextItem("##graph_ctx"))
    {
        if (ImGui::MenuItem(ICON_FA_PLUS "  New Zone"))
            (void)WorldDoc.AddZone(graph.Id, "New Zone");
        if (ImGui::MenuItem(ICON_FA_PEN "  Rename"))
        {
            RenamingGraph_ = graph.Id;
            std::strncpy(RenameBuffer_, graph.Name.c_str(), sizeof(RenameBuffer_) - 1);
            RenameBuffer_[sizeof(RenameBuffer_) - 1] = '\0';
        }
        ImGui::EndPopup();
    }

    if (open)
    {
        DrawGraphStreaming(graph);
        for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
        {
            if (zone.Graph == graph.Id)
                DrawZoneRow(zone);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

namespace
{

// Starter radius when a graph switches to the Proximity shape: the largest
// horizontal half-extent among the graph's zone bounds, so from a cell
// center the first preview sphere reaches the neighboring cells' near faces
// and the designer tunes from something visible. 100 when the graph has no
// measurable zones yet.
double SeedGraphRadius(const WorldPartitionManifest& manifest, GraphId graph)
{
    double largest = 0.0;
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (zone.Graph != graph || !zone.Bounds.IsValid())
            continue;
        const Vec3d extent = zone.Bounds.Extent();
        largest = std::max({ largest, static_cast<double>(extent[0]),
                             static_cast<double>(extent[2]) });
    }
    return largest > 0.0 ? largest : 100.0;
}

} // namespace

void WorldPartitionPanel::DrawGraphStreaming(const GraphRecord& graph)
{
    ImGui::PushID("graph_streaming");

    const WorldPartitionStreamingConfig base{};
    const bool proximity = graph.Streaming.Radius.value_or(base.Radius) > 0.0;
    const bool inheritedShape = !graph.Streaming.Radius.has_value();

    // The shape combo is presentation over the radius value: Graph authors an
    // explicit 0, Proximity authors a starter radius, Inherited clears the
    // field. Nothing stores a mode; the runtime reads only the values.
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("Streaming",
                          GraphStreamingShapeLabel(graph.Streaming, base.Radius)))
    {
        const auto option = [&](const char* label, bool selected, const char* help,
                                auto apply)
        {
            if (ImGui::Selectable(label, selected) && !selected)
                apply();
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 280.0f);
            ImGui::TextDisabled("%s", help);
            ImGui::PopTextWrapPos();
        };
        option("Inherited", inheritedShape,
               "Use the world's default shape.",
               [&] { (void)WorldDoc.SetGraphRadius(graph.Id, std::nullopt); });
        option("Graph", !inheritedShape && !proximity,
               "Zones load through authored connections: rooms behind doorways. "
               "Only connected zones preload; distance never matters.",
               [&] { (void)WorldDoc.SetGraphRadius(graph.Id, 0.0); });
        option("Proximity", !inheritedShape && proximity,
               "Zones load by distance from the player: an open area tiled into "
               "grid cells needs no connections, and diagonal neighbors load too.",
               [&]
               {
                   (void)WorldDoc.SetGraphRadius(
                       graph.Id, SeedGraphRadius(WorldDoc.Manifest(), graph.Id));
               });
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How this graph decides which zones load around the player");

    // Inherited fields display the engine-default base; an edit authors the
    // override, the clear button returns the field to inherited. Clamps
    // mirror the validation bounds so the editors cannot author an invalid
    // value (hand-edited manifests still validate).
    const auto clearOrInherited = [&](bool authored, const char* id, auto clear)
    {
        ImGui::SameLine();
        if (!authored)
        {
            ImGui::TextColored(EditorUi::TextDim, "(inherited)");
            return;
        }
        ImGui::PushID(id);
        if (ImGui::SmallButton(ICON_FA_XMARK))
            clear();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Clear back to inherited");
        ImGui::PopID();
    };

    // The radius only exists for a proximity shape; the combo owns entering
    // and leaving it. Committed on deactivate: a per-keystroke commit would
    // flip the shape to Graph (and hide this field) the moment a typed value
    // passes through 0.
    if (proximity)
    {
        ImGui::SetNextItemWidth(70.0f);
        float radius = static_cast<float>(graph.Streaming.Radius.value_or(base.Radius));
        (void)ImGui::InputFloat("Load radius", &radius, 0.0f, 0.0f, "%.0f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            (void)WorldDoc.SetGraphRadius(graph.Id,
                                           radius < 0.0f ? 0.0 : static_cast<double>(radius));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("World units. Zones within this distance of the player load, "
                              "connected or not. Drawn as a circle in the streaming preview.");
    }

    ImGui::SetNextItemWidth(70.0f);
    int hops = graph.Streaming.HopCount.value_or(base.HopCount);
    if (ImGui::InputInt("Preload hops", &hops))
        (void)WorldDoc.SetGraphHopCount(graph.Id, hops < 0 ? 0 : hops);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How many connections ahead zones preload: 1 keeps every "
                          "adjacent room loaded, 2 the rooms behind those. Connections "
                          "still apply in a Proximity graph (its entrances).");
    clearOrInherited(graph.Streaming.HopCount.has_value(), "clear_hops",
                     [&] { (void)WorldDoc.SetGraphHopCount(graph.Id, std::nullopt); });

    ImGui::SetNextItemWidth(70.0f);
    int cap = graph.Streaming.ResidentZoneCap.value_or(base.ResidentZoneCap);
    if (ImGui::InputInt("Zone cap", &cap))
        (void)WorldDoc.SetGraphResidentCap(graph.Id, cap < 1 ? 1 : cap);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Most zones kept loaded at once while the player is in this "
                          "graph; the farthest preloads unload first. The player's zone "
                          "and pinned zones never unload.");
    clearOrInherited(graph.Streaming.ResidentZoneCap.has_value(), "clear_cap",
                     [&] { (void)WorldDoc.SetGraphResidentCap(graph.Id, std::nullopt); });

    ImGui::PopID();
}

void WorldPartitionPanel::DrawZoneRow(const ZoneHeader& zone)
{
    ImGui::PushID(static_cast<int>(zone.Id.Value & 0x7fffffff));

    const bool isFocus = WorldDoc.FocusZone() == zone.Id;
    const bool isOpen = WorldDoc.IsZoneOpen(zone.Id);

    bool visible = true;
    bool dirty = false;
    if (isOpen)
    {
        WorldDoc.VisitOpenZones([&](ZoneId id, EditorDocument& document, const ZoneViewState& view)
                                {
                                    if (id != zone.Id)
                                        return;
                                    visible = view.VisibleInEditor;
                                    dirty = document.IsDirty();
                                });
    }

    if (RenamingZone_ == zone.Id)
    {
        DrawRenameField(true);
        if (ImGui::IsItemDeactivated())
        {
            if (RenameBuffer_[0] != '\0')
                (void)WorldDoc.RenameZone(zone.Id, RenameBuffer_);
            RenamingZone_ = ZoneId{};
        }
        ImGui::PopID();
        return;
    }

    if (isOpen)
    {
        if (ImGui::SmallButton(visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH))
            (void)WorldDoc.SetZoneVisible(zone.Id, !visible);
        ImGui::SameLine();
    }

    // State badge: F focus, C context (open), H hidden; header-only rows get none.
    if (isFocus)
        ImGui::TextColored(EditorUi::Accent, "F");
    else if (isOpen && !visible)
        ImGui::TextColored(EditorUi::TextDim, "H");
    else if (isOpen)
        ImGui::TextColored(EditorUi::TextPrimary, "C");
    if (isFocus || isOpen)
        ImGui::SameLine();

    ImGui::TextColored(zone.BoundsOverridden ? EditorUi::Warning : EditorUi::TextDim,
                       zone.BoundsOverridden ? "AABB*" : "AABB");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(zone.BoundsOverridden
            ? "Explicit AABB override. Clear the entity selection and use Resize to edit it."
            : "AABB derived from open zone content. Clear the entity selection and use Resize to override it.");
    ImGui::SameLine();

    const bool headerOnly = !isOpen;
    if (headerOnly)
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);

    std::string label = zone.Name;
    if (dirty)
        label += " " ICON_FA_CIRCLE_DOT;
    label += "##zone_row";
    const bool selected = WorldDoc.SelectedZone() == zone.Id;
    const bool clicked = ImGui::Selectable(
        label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick);
    if (headerOnly)
        ImGui::PopStyleColor();

    if (clicked)
    {
        Selection.ClearSelection();
        (void)WorldDoc.SelectZone(zone.Id);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            (void)WorldDoc.SetFocusZone(zone.Id);
    }

    if (ImGui::BeginPopupContextItem("##zone_ctx"))
    {
        if (ImGui::MenuItem("Focus"))
            (void)WorldDoc.SetFocusZone(zone.Id);
        if (ImGui::MenuItem("Load As Context", nullptr, false, !isOpen))
            (void)WorldDoc.LoadZone(zone.Id);
        const bool canUnload = isOpen && !dirty && !isFocus;
        if (ImGui::MenuItem("Unload", nullptr, false, canUnload))
            (void)WorldDoc.UnloadZone(zone.Id);
        if (isOpen && dirty && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Save the zone first");

        std::vector<EntityId> selectedEntities;
        for (const SelectableRef& ref : Selection.GetSelection())
            if (ref.IsEntity())
                selectedEntities.push_back(ref.Entity);
        const bool canReceive = isOpen && !isFocus && !selectedEntities.empty();
        if (ImGui::MenuItem("Move Selection Here", nullptr, false, canReceive))
        {
            if (auto command = MakeMoveEntitiesToZoneCommand(selectedEntities, WorldDoc,
                                                             zone.Id, Selection))
            {
                Commands.Execute(std::move(command));
                WorldDoc.Logging().GetLogger<WorldPartitionPanel>().Info(
                    "moved {} entities to {}", selectedEntities.size(), zone.Name);
            }
        }
        if (ImGui::MenuItem(ICON_FA_PEN "  Rename"))
        {
            RenamingZone_ = zone.Id;
            std::strncpy(RenameBuffer_, zone.Name.c_str(), sizeof(RenameBuffer_) - 1);
            RenameBuffer_[sizeof(RenameBuffer_) - 1] = '\0';
        }
        if (zone.BoundsOverridden
            && ImGui::MenuItem("Use Derived AABB", nullptr, false, isOpen))
        {
            Aabb3d derived = zone.Bounds;
            if (EditorDocument* document = WorldDoc.ZoneDocument(zone.Id))
                if (const auto bounds = ComputeZoneBounds(document->GetScene()))
                    derived = *bounds;
            Commands.Execute(std::make_unique<SetZoneBoundsCommand>(
                WorldDoc, zone.Id, zone.Bounds, true, derived, false));
        }
        if (ImGui::BeginMenu("Move To Graph"))
        {
            for (const GraphRecord& graph : WorldDoc.Manifest().Graphs)
            {
                if (ImGui::MenuItem(graph.Name.c_str(), nullptr, graph.Id == zone.Graph,
                                    graph.Id != zone.Graph))
                    (void)WorldDoc.SetZoneGraph(zone.Id, graph.Id);
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

void WorldPartitionPanel::DrawValidation()
{
    const auto records = WorldDoc.ValidationRecords();
    if (records.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
        ImGui::TextUnformatted("Validation: clean");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::TextUnformatted("Validation");
    int rowId = 0;
    for (const ContentRiskRecord& record : records)
    {
        ImGui::PushID(rowId++);
        switch (record.Severity)
        {
        case ContentRiskSeverity::Error:
            ImGui::TextColored(EditorUi::Danger, ICON_FA_CIRCLE_XMARK);
            break;
        case ContentRiskSeverity::Warning:
            ImGui::TextColored(EditorUi::Warning, ICON_FA_TRIANGLE_EXCLAMATION);
            break;
        }
        ImGui::SameLine();
        const std::string label = record.RuleId + "##validation_row";
        if (ImGui::Selectable(label.c_str()))
            NavigateToRecord(record);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", record.Message.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
        ImGui::TextWrapped("%s", record.Message.c_str());
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

void WorldPartitionPanel::NavigateToRecord(const ContentRiskRecord& record)
{
    if (record.Kind == ContentRiskSourceKind::Graph)
    {
        NavigateGraph_ = GraphId{ record.SourceId };
        return;
    }

    ZoneId zone{};
    if (record.Kind == ContentRiskSourceKind::Transition)
    {
        const TransitionId transition{ record.SourceId };
        for (const TransitionRecord& candidate : WorldDoc.Manifest().Transitions)
            if (candidate.Id == transition)
                zone = candidate.From;
        if (!zone.IsValid())
            return;
    }
    else if (record.Kind == ContentRiskSourceKind::Zone)
    {
        zone = ZoneId{ record.SourceId };
    }
    else
    {
        return;
    }

    Selection.ClearSelection();
    (void)WorldDoc.SelectZone(zone);
    for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
    {
        if (header.Id == zone)
        {
            NavigateGraph_ = header.Graph;
            return;
        }
    }
}
