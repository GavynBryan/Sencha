#include "WorldPartitionPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "fonts/IconsFontAwesome6.h"

#include "commands/CommandStack.h"
#include "document/TransitionConnect.h"
#include "document/WorldDocument.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"
#include "selection/SelectionService.h"
#include "ui/TransitionInlineEditor.h"
#include "viewport/WorldViewSettings.h"

#include <core/logging/LoggingProvider.h>
#include <zone/ZoneDemand.h>

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <vector>

WorldPartitionPanel::WorldPartitionPanel(WorldDocument& world, SelectionService& selection,
                                         CommandStack& commands)
    : WorldDoc(world)
    , Selection(selection)
    , Commands(commands)
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

    // Deferred connect from a zone row's Connect To submenu (see the header).
    if (PendingConnectFrom_.IsValid())
    {
        ZoneId target = PendingConnectTo_;
        if (!target.IsValid() && PendingConnectNewRegion_.IsValid())
            target = WorldDoc.AddZone(PendingConnectNewRegion_, "New Zone");
        if (target.IsValid())
            SelectedTransitionRow_ =
                ConnectZones(WorldDoc, PendingConnectFrom_, target, /*oneWay*/ false);
        PendingConnectFrom_ = ZoneId{};
        PendingConnectTo_ = ZoneId{};
        PendingConnectNewRegion_ = RegionId{};
    }

    DrawHeaderButtons();
    ImGui::Separator();

    for (const RegionRecord& region : WorldDoc.Manifest().Regions)
        DrawRegion(region);

    // Zones whose region reference resolves to no region record still need a
    // home in the tree, or they would be unreachable for repair.
    bool orphanHeader = false;
    for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
    {
        const bool known = [&]
        {
            for (const RegionRecord& region : WorldDoc.Manifest().Regions)
                if (region.Id == zone.Region)
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

    DrawConnections();
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

    // The config in force: focus selects the region whose shape is active.
    // This line is what makes that rule legible at region boundaries.
    const RegionRecord* focusRegion = nullptr;
    for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
        if (header.Id == previewFocus)
            for (const RegionRecord& region : WorldDoc.Manifest().Regions)
                if (region.Id == header.Region)
                    focusRegion = &region;
    const WorldPartitionStreamingConfig resolved = ResolveRegionStreamingConfig(
        WorldDoc.Manifest(), previewFocus, WorldPartitionStreamingConfig{});
    {
        const RegionStreamingConfig authored =
            focusRegion != nullptr ? focusRegion->Streaming : RegionStreamingConfig{};
        std::string line = focusRegion != nullptr
            ? std::format("Shape from \"{}\":", focusRegion->Name)
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

    // Scratch world tags: preview how gated connections reflow without play.
    char tagBuffer[256];
    std::strncpy(tagBuffer, view->PreviewTags.c_str(), sizeof(tagBuffer) - 1);
    tagBuffer[sizeof(tagBuffer) - 1] = '\0';
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##preview_tags", tagBuffer, sizeof(tagBuffer)))
        view->PreviewTags = tagBuffer;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Active world tags for the preview (comma separated)");

    const auto zoneName = [&](ZoneId zone) -> const char*
    {
        for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
            if (header.Id == zone)
                return header.Name.c_str();
        return "<unknown>";
    };

    const std::vector<std::string> activeTags = SplitTagList(view->PreviewTags);
    const auto records = ComputeZoneDemand(
        WorldDoc.Manifest(), WorldDoc.Index(), previewFocus, {},
        ResolvePreviewStreamingConfig(WorldDoc.Manifest(), previewFocus, *view),
        &view->PreviewFocusPosition, activeTags);
    for (const ZoneDemandRecord& record : records)
    {
        std::string why;
        const auto tag = [&](bool on, const char* name)
        {
            if (!on)
                return;
            if (!why.empty())
                why += "+";
            why += name;
        };
        tag(record.Sources.Focus, "focus");
        tag(record.Sources.Neighbor, "neighbor");
        tag(record.Sources.Spatial, "near");
        why += record.Sources.Focus ? ", live"
             : record.Desired.Visible ? ", render preload"
                                      : ", dormant preload";
        ImGui::BulletText("%s (%s)", zoneName(record.Zone), why.c_str());
    }
    const size_t total = WorldDoc.Manifest().Zones.size();
    if (total > records.size())
        ImGui::TextDisabled("%zu of %zu zones stay unloaded", total - records.size(), total);
}

void WorldPartitionPanel::DrawHeaderButtons()
{
    if (ImGui::Button(ICON_FA_PLUS "  Region"))
        (void)WorldDoc.AddRegion("New Region");

    // New zones land in the focus zone's region (fallback: the first region).
    RegionId zoneRegion;
    for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
        if (zone.Id == WorldDoc.FocusZone())
            zoneRegion = zone.Region;
    if (!zoneRegion.IsValid() && !WorldDoc.Manifest().Regions.empty())
        zoneRegion = WorldDoc.Manifest().Regions[0].Id;
    ImGui::SameLine();
    ImGui::BeginDisabled(!zoneRegion.IsValid());
    if (ImGui::Button(ICON_FA_PLUS "  Zone"))
        (void)WorldDoc.AddZone(zoneRegion, "New Zone");
    ImGui::EndDisabled();
}

void WorldPartitionPanel::DrawConnections()
{
    ImGui::Separator();
    ImGui::TextColored(EditorUi::Accent, ICON_FA_ARROW_RIGHT "  Connections");

    // One row per connection: a symmetric pair (swapped endpoints, same
    // topology, both two-way) collapses into a single undirected row keyed by
    // its lower-id edge. Ids copied first: row menus edit the vector.
    struct ConnectionRow
    {
        TransitionId Representative;
        TransitionId Partner;   // invalid for a one-way or unpaired edge
    };
    std::vector<ConnectionRow> rows;
    std::vector<uint64_t> consumed;
    for (const TransitionRecord& record : WorldDoc.Manifest().Transitions)
    {
        bool skip = false;
        for (uint64_t id : consumed)
            skip |= id == record.Id.Value;
        if (skip)
            continue;
        ConnectionRow row{ record.Id, TransitionId{} };
        if (!record.Flags.OneWay)
        {
            for (const TransitionRecord& other : WorldDoc.Manifest().Transitions)
            {
                if (other.Id == record.Id || other.Flags.OneWay
                    || other.Topology != record.Topology || other.From != record.To
                    || other.To != record.From)
                    continue;
                row.Partner = other.Id;
                consumed.push_back(other.Id.Value);
                break;
            }
        }
        rows.push_back(row);
    }

    if (rows.empty())
        ImGui::TextDisabled("None: use Connect on a zone row");
    for (const ConnectionRow& row : rows)
        DrawConnectionRow(row.Representative, row.Partner);
}

void WorldPartitionPanel::DrawConnectionRow(TransitionId representative, TransitionId partner)
{
    const auto findRecord = [&](TransitionId id) -> const TransitionRecord*
    {
        for (const TransitionRecord& record : WorldDoc.Manifest().Transitions)
            if (record.Id == id)
                return &record;
        return nullptr;
    };
    const TransitionRecord* record = findRecord(representative);
    if (record == nullptr)
        return;

    ImGui::PushID(static_cast<int>(representative.Value & 0x7fffffff));

    if (RenamingTransition_ == representative)
    {
        DrawRenameField(true);
        if (ImGui::IsItemDeactivated())
        {
            // Both directions carry the connection's name.
            (void)WorldDoc.RenameTransition(representative, RenameBuffer_);
            if (partner.IsValid())
                (void)WorldDoc.RenameTransition(partner, RenameBuffer_);
            RenamingTransition_ = TransitionId{};
        }
        ImGui::PopID();
        return;
    }

    // Inline severity: the worst transition-kind record naming either edge.
    const ContentRiskRecord* worst = nullptr;
    for (const ContentRiskRecord& riskRecord : WorldDoc.ValidationRecords())
    {
        if (riskRecord.Kind != ContentRiskSourceKind::Transition
            || (riskRecord.SourceId != representative.Value
                && riskRecord.SourceId != partner.Value))
            continue;
        if (worst == nullptr || riskRecord.Severity == ContentRiskSeverity::Error)
            worst = &riskRecord;
    }
    if (worst != nullptr)
    {
        switch (worst->Severity)
        {
        case ContentRiskSeverity::Error:
            ImGui::TextColored(EditorUi::Danger, ICON_FA_CIRCLE_XMARK);
            break;
        case ContentRiskSeverity::Warning:
            ImGui::TextColored(EditorUi::Warning, ICON_FA_TRIANGLE_EXCLAMATION);
            break;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", worst->Message.c_str());
        ImGui::SameLine();
    }

    const char* badge = "D";
    const char* badgeName = "Doorway";
    if (record->Topology == TransitionTopology::Seam)
    {
        badge = "S";
        badgeName = "Seam";
    }
    else if (record->Topology == TransitionTopology::Teleport)
    {
        badge = "T";
        badgeName = "Teleport";
    }
    ImGui::TextColored(EditorUi::TextDim, "%s", badge);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s: %s", badgeName, TransitionTopologyHelp(record->Topology));
    ImGui::SameLine();

    const auto zoneName = [&](ZoneId zone) -> std::string
    {
        for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
            if (header.Id == zone)
                return header.Name;
        return ZoneIdToString(zone);
    };
    std::string label;
    if (!record->Name.empty())
        label = record->Name;
    else
        label = zoneName(record->From)
              + (partner.IsValid() ? " " ICON_FA_ARROWS_LEFT_RIGHT " " : " " ICON_FA_ARROW_RIGHT " ")
              + zoneName(record->To);
    if (!record->RequiredTags.empty())
        label += "  " ICON_FA_LOCK;
    label += "##connection_row";
    const bool highlighted = SelectedTransitionRow_ == representative
        || (partner.IsValid() && SelectedTransitionRow_ == partner);
    ImGui::Selectable(label.c_str(), highlighted);

    if (ImGui::BeginPopupContextItem("##connection_ctx"))
    {
        // Every property edit applies to both directions of a pair: a
        // connection is one thing, however the streaming graph stores it.
        DrawTransitionInlineEditor(WorldDoc, representative, partner);
        ImGui::Separator();

        if (partner.IsValid())
        {
            if (ImGui::MenuItem("Make One-way"))
            {
                (void)WorldDoc.RemoveTransition(partner);
                (void)WorldDoc.SetTransitionOneWay(representative, true);
            }
        }
        else if (ImGui::MenuItem("Make Two-way"))
        {
            (void)WorldDoc.SetTransitionOneWay(representative, false);
            const TransitionId reverse = WorldDoc.AddTransition(
                record->To, record->From, record->Topology, false, record->PreloadPriority);
            (void)WorldDoc.SetTransitionPreloadDepth(reverse, record->PreloadDepth);
            (void)WorldDoc.SetTransitionRequiredTags(reverse, record->RequiredTags);
        }

        if (ImGui::MenuItem(ICON_FA_PEN "  Rename"))
        {
            RenamingTransition_ = representative;
            std::strncpy(RenameBuffer_, record->Name.c_str(), sizeof(RenameBuffer_) - 1);
            RenameBuffer_[sizeof(RenameBuffer_) - 1] = '\0';
        }

        if (ImGui::BeginMenu(ICON_FA_TRASH "  Remove"))
        {
            if (ImGui::MenuItem("Confirm Remove"))
            {
                (void)WorldDoc.RemoveTransition(representative);
                if (partner.IsValid())
                    (void)WorldDoc.RemoveTransition(partner);
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
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

void WorldPartitionPanel::DrawRegion(const RegionRecord& region)
{
    ImGui::PushID(static_cast<int>(region.Id.Value & 0x7fffffff));

    if (RenamingRegion_ == region.Id)
    {
        DrawRenameField(true);
        if (ImGui::IsItemDeactivated())
        {
            if (RenameBuffer_[0] != '\0')
                (void)WorldDoc.RenameRegion(region.Id, RenameBuffer_);
            RenamingRegion_ = RegionId{};
        }
        ImGui::PopID();
        return;
    }

    if (NavigateRegion_ == region.Id)
    {
        ImGui::SetNextItemOpen(true);
        NavigateRegion_ = RegionId{};
    }
    const bool open = ImGui::TreeNodeEx(region.Name.c_str(),
                                        ImGuiTreeNodeFlags_DefaultOpen
                                            | ImGuiTreeNodeFlags_SpanAvailWidth);

    if (ImGui::BeginPopupContextItem("##region_ctx"))
    {
        if (ImGui::MenuItem(ICON_FA_PLUS "  New Zone"))
            (void)WorldDoc.AddZone(region.Id, "New Zone");
        if (ImGui::MenuItem(ICON_FA_PEN "  Rename"))
        {
            RenamingRegion_ = region.Id;
            std::strncpy(RenameBuffer_, region.Name.c_str(), sizeof(RenameBuffer_) - 1);
            RenameBuffer_[sizeof(RenameBuffer_) - 1] = '\0';
        }
        ImGui::EndPopup();
    }

    if (open)
    {
        DrawRegionStreaming(region);
        for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
        {
            if (zone.Region == region.Id)
                DrawZoneRow(zone);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

namespace
{

// Starter radius when a region switches to the Proximity shape: the largest
// horizontal half-extent among the region's zone bounds, so from a cell
// center the first preview sphere reaches the neighboring cells' near faces
// and the designer tunes from something visible. 100 when the region has no
// measurable zones yet.
double SeedRegionRadius(const WorldPartitionManifest& manifest, RegionId region)
{
    double largest = 0.0;
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (zone.Region != region || !zone.Bounds.IsValid())
            continue;
        const Vec3d extent = zone.Bounds.Extent();
        largest = std::max({ largest, static_cast<double>(extent[0]),
                             static_cast<double>(extent[2]) });
    }
    return largest > 0.0 ? largest : 100.0;
}

} // namespace

void WorldPartitionPanel::DrawRegionStreaming(const RegionRecord& region)
{
    ImGui::PushID("region_streaming");

    const WorldPartitionStreamingConfig base{};
    const bool proximity = region.Streaming.Radius.value_or(base.Radius) > 0.0;
    const bool inheritedShape = !region.Streaming.Radius.has_value();

    // The shape combo is presentation over the radius value: Graph authors an
    // explicit 0, Proximity authors a starter radius, Inherited clears the
    // field. Nothing stores a mode; the runtime reads only the values.
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("Streaming",
                          RegionStreamingShapeLabel(region.Streaming, base.Radius)))
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
               [&] { (void)WorldDoc.SetRegionRadius(region.Id, std::nullopt); });
        option("Graph", !inheritedShape && !proximity,
               "Zones load through authored connections: rooms behind doorways. "
               "Only connected zones preload; distance never matters.",
               [&] { (void)WorldDoc.SetRegionRadius(region.Id, 0.0); });
        option("Proximity", !inheritedShape && proximity,
               "Zones load by distance from the player: an open area tiled into "
               "grid cells needs no connections, and diagonal neighbors load too.",
               [&]
               {
                   (void)WorldDoc.SetRegionRadius(
                       region.Id, SeedRegionRadius(WorldDoc.Manifest(), region.Id));
               });
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How this region decides which zones load around the player");

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
        float radius = static_cast<float>(region.Streaming.Radius.value_or(base.Radius));
        (void)ImGui::InputFloat("Load radius", &radius, 0.0f, 0.0f, "%.0f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            (void)WorldDoc.SetRegionRadius(region.Id,
                                           radius < 0.0f ? 0.0 : static_cast<double>(radius));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("World units. Zones within this distance of the player load, "
                              "connected or not. Drawn as a circle in the streaming preview.");
    }

    ImGui::SetNextItemWidth(70.0f);
    int hops = region.Streaming.HopCount.value_or(base.HopCount);
    if (ImGui::InputInt("Preload hops", &hops))
        (void)WorldDoc.SetRegionHopCount(region.Id, hops < 0 ? 0 : hops);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How many connections ahead zones preload: 1 keeps every "
                          "adjacent room loaded, 2 the rooms behind those. Connections "
                          "still apply in a Proximity region (its entrances).");
    clearOrInherited(region.Streaming.HopCount.has_value(), "clear_hops",
                     [&] { (void)WorldDoc.SetRegionHopCount(region.Id, std::nullopt); });

    ImGui::SetNextItemWidth(70.0f);
    int cap = region.Streaming.ResidentZoneCap.value_or(base.ResidentZoneCap);
    if (ImGui::InputInt("Zone cap", &cap))
        (void)WorldDoc.SetRegionResidentCap(region.Id, cap < 1 ? 1 : cap);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Most zones kept loaded at once while the player is in this "
                          "region; the farthest preloads unload first. The player's zone "
                          "and pinned zones never unload.");
    clearOrInherited(region.Streaming.ResidentZoneCap.has_value(), "clear_cap",
                     [&] { (void)WorldDoc.SetRegionResidentCap(region.Id, std::nullopt); });

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

    const bool headerOnly = !isOpen;
    if (headerOnly)
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);

    std::string label = zone.Name;
    if (dirty)
        label += " " ICON_FA_CIRCLE_DOT;
    label += "##zone_row";
    const bool selected = isFocus || SelectedZoneRow_ == zone.Id;
    ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick);
    if (headerOnly)
        ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        (void)WorldDoc.SetFocusZone(zone.Id);

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
        if (ImGui::BeginMenu(ICON_FA_ARROW_RIGHT "  Connect To"))
        {
            // One click mints a two-way Doorway pair with defaults; the
            // transition row's menu adjusts topology/one-way/priority after.
            for (const ZoneHeader& target : WorldDoc.Manifest().Zones)
            {
                if (target.Id == zone.Id)
                    continue;
                if (ImGui::MenuItem(target.Name.c_str()))
                {
                    PendingConnectFrom_ = zone.Id;
                    PendingConnectTo_ = target.Id;
                }
            }
            ImGui::Separator();
            for (const RegionRecord& region : WorldDoc.Manifest().Regions)
            {
                const std::string label = "New Zone In " + region.Name;
                if (ImGui::MenuItem(label.c_str()))
                {
                    PendingConnectFrom_ = zone.Id;
                    PendingConnectNewRegion_ = region.Id;
                }
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
    if (record.Kind == ContentRiskSourceKind::Region)
    {
        NavigateRegion_ = RegionId{ record.SourceId };
        return;
    }

    // Transition records navigate to the From zone's row and highlight the
    // transition child row under it.
    ZoneId zone{};
    if (record.Kind == ContentRiskSourceKind::Transition)
    {
        const TransitionId transition{ record.SourceId };
        for (const TransitionRecord& candidate : WorldDoc.Manifest().Transitions)
            if (candidate.Id == transition)
                zone = candidate.From;
        if (!zone.IsValid())
            return;
        SelectedTransitionRow_ = transition;
    }
    else if (record.Kind == ContentRiskSourceKind::Zone)
    {
        zone = ZoneId{ record.SourceId };
        SelectedTransitionRow_ = TransitionId{};
    }
    else
    {
        return;
    }

    SelectedZoneRow_ = zone;
    for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
    {
        if (header.Id == zone)
        {
            NavigateRegion_ = header.Region;
            return;
        }
    }
}
