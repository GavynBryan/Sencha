#include "WorldPartitionPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "fonts/IconsFontAwesome6.h"

#include "commands/CommandStack.h"
#include "document/PortalGeometry.h"
#include "document/TransitionConnect.h"
#include "document/WorldDocument.h"
#include "document/commands/CreateEntityCommand.h"
#include "document/commands/LinkPortalCommand.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"
#include "meshedit/ElementGeometry.h"
#include "selection/SelectionService.h"
#include "selection/commands/SelectCommand.h"

#include <core/logging/LoggingProvider.h>

#include <imgui.h>

#include <cstring>
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
            SelectedTransitionRow_ = ConnectZones(WorldDoc, PendingConnectFrom_, target,
                                                  /*oneWay*/ false, PendingConnectPortal_,
                                                  Commands);
        PendingConnectFrom_ = ZoneId{};
        PendingConnectTo_ = ZoneId{};
        PendingConnectNewRegion_ = RegionId{};
        PendingConnectPortal_ = EntityId{};
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

    DrawConnectBar();

    ImGui::Separator();
    DrawValidation();
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

    // A ready-made portal marker: fitted over the selected face (the doorway
    // cut), else a default thin box at the origin for the manipulators. The
    // new portal is selected, so the connect bar appears immediately.
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS "  Portal"))
    {
        EditorDocument& document = WorldDoc.FocusDocument();
        EditorScene& scene = document.GetScene();
        Vec3d center{ 0.0, 0.0, 0.0 };
        Vec3d halfExtents{ 2.0, 2.0, 0.125 };

        const auto refs = Selection.GetSelection();
        if (refs.size() == 1 && refs[0].IsFace())
        {
            const BrushMesh* mesh = scene.TryGetBrushMesh(refs[0].Entity);
            const Transform3f* transform = scene.TryGetTransform(refs[0].Entity);
            if (mesh != nullptr && transform != nullptr
                && refs[0].ElementId < mesh->Faces.size())
            {
                std::vector<Vec3d> worldVertices;
                for (uint32_t index : ElementVertexIndices(*mesh, *transform, refs[0]))
                    worldVertices.push_back(
                        transform->TransformPoint(mesh->Vertices[index].Position));
                if (!worldVertices.empty())
                {
                    const Vec3d normal = transform->Rotation.RotateVector(
                        BrushComputeFaceNormal(*mesh, mesh->Faces[refs[0].ElementId]));
                    const PortalBoxFit fit =
                        FitPortalBoxToFace(worldVertices, normal, /*thickness*/ 0.25);
                    center = fit.Center;
                    halfExtents = fit.HalfExtents;
                }
            }
        }

        auto create = MakeCreatePortalBrushCommand(center, halfExtents, scene, document);
        CreateEntityCommand* command = create.get();
        Commands.Execute(std::move(create));
        Commands.Execute(std::make_unique<SelectCommand>(
            Selection, SelectableRef::EntitySelection(scene.GetRegistry().Id,
                                                      command->GetCreatedEntity())));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Select the opening's face first to fit the portal into it");
}

void WorldPartitionPanel::DrawConnectBar()
{
    const auto refs = Selection.GetSelection();
    if (refs.size() != 1 || !refs[0].IsEntity())
        return;
    const EntityId entity = refs[0].Entity;
    const EditorScene& scene = WorldDoc.FocusDocument().GetScene();
    const PortalComponent* portal = scene.TryGetPortal(entity);
    if (portal == nullptr)
        return;

    const auto zoneName = [&](ZoneId zone) -> const char*
    {
        for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
            if (header.Id == zone)
                return header.Name.c_str();
        return "<unknown zone>";
    };
    const auto navigateTo = [&](const TransitionRecord& record)
    {
        SelectedTransitionRow_ = record.Id;
        SelectedZoneRow_ = record.From;
        for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
            if (header.Id == record.From)
                NavigateRegion_ = header.Region;
    };

    ImGui::Separator();

    if (portal->Transition.IsValid())
    {
        const TransitionRecord* record = nullptr;
        for (const TransitionRecord& candidate : WorldDoc.Manifest().Transitions)
            if (candidate.Id == portal->Transition)
                record = &candidate;
        if (record != nullptr)
        {
            ImGui::Text("Portal linked " ICON_FA_ARROW_RIGHT "  %s", zoneName(record->To));
            ImGui::SameLine();
            if (ImGui::SmallButton("Show"))
                navigateTo(*record);
        }
        else
        {
            ImGui::TextColored(EditorUi::Warning, "Portal linked to a removed transition");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Unlink"))
        {
            if (auto unlink = MakeLinkPortalCommand(WorldDoc.FocusDocument(), entity,
                                                    TransitionId{}))
            {
                Commands.Execute(std::move(unlink));
                WorldDoc.Revalidate();
            }
        }
        return;
    }

    // Unlinked: the one-click connect flow, target pre-guessed from the
    // portal's facing when the selection lands on it.
    if (ConnectBarEntity_ != entity)
    {
        ConnectBarEntity_ = entity;
        ConnectBarTwoWay_ = true;
        ConnectBarTarget_ = ZoneId{};
        if (const auto bounds = scene.TryGetWorldBounds(entity))
            ConnectBarTarget_ =
                GuessPortalTargetZone(WorldDoc.Manifest(), WorldDoc.FocusZone(), *bounds);
        if (!ConnectBarTarget_.IsValid())
            for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
                if (header.Id != WorldDoc.FocusZone())
                {
                    ConnectBarTarget_ = header.Id;
                    break;
                }
    }

    ImGui::Text("Connect %s " ICON_FA_ARROW_RIGHT, zoneName(WorldDoc.FocusZone()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##connect_target", ConnectBarTarget_.IsValid()
                                                  ? zoneName(ConnectBarTarget_)
                                                  : "<no other zone>"))
    {
        for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
        {
            if (header.Id == WorldDoc.FocusZone())
                continue;
            if (ImGui::Selectable(header.Name.c_str(), header.Id == ConnectBarTarget_))
                ConnectBarTarget_ = header.Id;
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Two-way", &ConnectBarTwoWay_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!ConnectBarTarget_.IsValid());
    if (ImGui::Button(ICON_FA_LINK "  Connect"))
    {
        const TransitionId forward = ConnectZones(WorldDoc, WorldDoc.FocusZone(),
                                                  ConnectBarTarget_, !ConnectBarTwoWay_,
                                                  entity, Commands);
        for (const TransitionRecord& record : WorldDoc.Manifest().Transitions)
            if (record.Id == forward)
                navigateTo(record);
    }
    ImGui::EndDisabled();
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
        for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
        {
            if (zone.Region == region.Id)
                DrawZoneRow(zone);
        }
        ImGui::TreePop();
    }

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
            // A selected portal in this zone auto-links (selection is
            // focus-zone-only by contract).
            EntityId linkEntity{};
            if (isFocus)
            {
                const auto refs = Selection.GetSelection();
                if (refs.size() == 1 && refs[0].IsEntity()
                    && WorldDoc.FocusDocument().GetScene().IsPortal(refs[0].Entity))
                    linkEntity = refs[0].Entity;
            }
            for (const ZoneHeader& target : WorldDoc.Manifest().Zones)
            {
                if (target.Id == zone.Id)
                    continue;
                if (ImGui::MenuItem(target.Name.c_str()))
                {
                    PendingConnectFrom_ = zone.Id;
                    PendingConnectTo_ = target.Id;
                    PendingConnectPortal_ = linkEntity;
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
                    PendingConnectPortal_ = linkEntity;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    // Ids copied first: a row's context menu can remove its transition, which
    // rebuilds the index and edits the vector the spans point into.
    std::vector<TransitionId> outgoing;
    for (uint32_t index : WorldDoc.Index().Outgoing(zone.Id))
        outgoing.push_back(WorldDoc.Manifest().Transitions[index].Id);
    for (TransitionId id : outgoing)
    {
        const TransitionRecord* record = nullptr;
        for (const TransitionRecord& candidate : WorldDoc.Manifest().Transitions)
            if (candidate.Id == id)
                record = &candidate;
        if (record == nullptr)
            continue;
        ImGui::Indent();
        DrawTransitionRow(*record);
        ImGui::Unindent();
    }

    ImGui::PopID();
}

void WorldPartitionPanel::DrawTransitionRow(const TransitionRecord& transition)
{
    ImGui::PushID(static_cast<int>(transition.Id.Value & 0x7fffffff));

    if (RenamingTransition_ == transition.Id)
    {
        DrawRenameField(true);
        if (ImGui::IsItemDeactivated())
        {
            // An emptied field clears the authored name back to the derived label.
            (void)WorldDoc.RenameTransition(transition.Id, RenameBuffer_);
            RenamingTransition_ = TransitionId{};
        }
        ImGui::PopID();
        return;
    }

    // Inline severity: the worst transition-kind record naming this edge.
    const ContentRiskRecord* worst = nullptr;
    for (const ContentRiskRecord& record : WorldDoc.ValidationRecords())
    {
        if (record.Kind != ContentRiskSourceKind::Transition
            || record.SourceId != transition.Id.Value)
            continue;
        if (worst == nullptr || record.Severity == ContentRiskSeverity::Error)
            worst = &record;
    }
    if (worst != nullptr)
    {
        switch (worst->Severity)
        {
        case ContentRiskSeverity::Error:
            ImGui::TextColored(EditorUi::Danger, ICON_FA_CIRCLE_XMARK);
            break;
        case ContentRiskSeverity::Unverified:
            ImGui::TextColored(EditorUi::TextDim, ICON_FA_CIRCLE_QUESTION);
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
    if (transition.Topology == TransitionTopology::Seam)
        badge = "S";
    else if (transition.Topology == TransitionTopology::Teleport)
        badge = "T";
    ImGui::TextColored(EditorUi::TextDim, "%s", badge);
    ImGui::SameLine();

    std::string rowText;
    if (!transition.Name.empty())
    {
        rowText = transition.Name;
    }
    else
    {
        rowText = ZoneIdToString(transition.To);
        for (const ZoneHeader& header : WorldDoc.Manifest().Zones)
            if (header.Id == transition.To)
                rowText = header.Name;
    }
    std::string label = std::string(ICON_FA_ARROW_RIGHT) + "  " + rowText;
    if (transition.Flags.OneWay)
        label += "  " ICON_FA_ARROW_RIGHT_LONG;
    label += "##transition_row";
    ImGui::Selectable(label.c_str(), SelectedTransitionRow_ == transition.Id);

    if (ImGui::BeginPopupContextItem("##transition_ctx"))
    {
        if (ImGui::BeginMenu("Topology"))
        {
            const auto item = [&](const char* name, TransitionTopology topology)
            {
                if (ImGui::MenuItem(name, nullptr, transition.Topology == topology))
                    (void)WorldDoc.SetTransitionTopology(transition.Id, topology);
            };
            item("Doorway", TransitionTopology::Doorway);
            item("Seam", TransitionTopology::Seam);
            item("Teleport", TransitionTopology::Teleport);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("One-Way", nullptr, transition.Flags.OneWay))
            (void)WorldDoc.SetTransitionOneWay(transition.Id, !transition.Flags.OneWay);
        int priority = transition.PreloadPriority;
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputInt("Preload Priority", &priority))
            (void)WorldDoc.SetTransitionPreloadPriority(transition.Id, priority);
        ImGui::Separator();

        const bool fromIsFocus = WorldDoc.FocusZone() == transition.From;
        EntityId selectedPortal{};
        if (fromIsFocus)
        {
            const auto refs = Selection.GetSelection();
            if (refs.size() == 1 && refs[0].IsEntity()
                && WorldDoc.FocusDocument().GetScene().IsPortal(refs[0].Entity))
                selectedPortal = refs[0].Entity;
        }
        if (ImGui::MenuItem("Link Selected Portal", nullptr, false, selectedPortal.IsValid()))
        {
            if (auto link = MakeLinkPortalCommand(WorldDoc.FocusDocument(), selectedPortal,
                                                  transition.Id))
            {
                Commands.Execute(std::move(link));
                WorldDoc.Revalidate();
            }
        }

        EntityId linkedPortal{};
        if (fromIsFocus)
        {
            const EditorScene& scene = WorldDoc.FocusDocument().GetScene();
            for (EntityId entity : scene.GetAllEntities())
                if (const PortalComponent* portal = scene.TryGetPortal(entity);
                    portal != nullptr && portal->Transition == transition.Id)
                {
                    linkedPortal = entity;
                    break;
                }
        }
        if (ImGui::MenuItem("Select Portal", nullptr, false, linkedPortal.IsValid()))
        {
            Commands.Execute(std::make_unique<SelectCommand>(
                Selection,
                SelectableRef::EntitySelection(
                    WorldDoc.FocusDocument().GetScene().GetRegistry().Id, linkedPortal)));
        }
        ImGui::Separator();

        if (ImGui::MenuItem(ICON_FA_PEN "  Rename"))
        {
            RenamingTransition_ = transition.Id;
            std::strncpy(RenameBuffer_, transition.Name.c_str(), sizeof(RenameBuffer_) - 1);
            RenameBuffer_[sizeof(RenameBuffer_) - 1] = '\0';
        }

        // Removes only this edge, never its derived reverse.
        if (ImGui::BeginMenu(ICON_FA_TRASH "  Remove"))
        {
            if (ImGui::MenuItem("Confirm Remove"))
                (void)WorldDoc.RemoveTransition(transition.Id);
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
        case ContentRiskSeverity::Unverified:
            ImGui::TextColored(EditorUi::TextDim, ICON_FA_CIRCLE_QUESTION);
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
