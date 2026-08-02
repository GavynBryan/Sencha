#include "SceneHierarchyPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "fonts/IconsFontAwesome6.h"

#include "commands/CommandStack.h"
#include "document/commands/CreateEntityCommand.h"
#include "document/commands/DeleteEntityCommand.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"
#include "selection/commands/SelectCommand.h"
#include "selection/SelectionService.h"

#include <core/logging/LoggingProvider.h>

#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializer.h>

#include <imgui.h>

#include <memory>
#include <span>
#include <string>
#include <vector>
#include "document/DocumentSerialization.h"

SceneHierarchyPanel::SceneHierarchyPanel(WorldDocument& world,
                                         SelectionService& selection, CommandStack& commands)
    : WorldDoc(world)
    , Selection(selection)
    , Commands(commands)
{
}

std::string_view SceneHierarchyPanel::GetTitle() const
{
    TitleCache = "Hierarchy";
    if (WorldDoc.IsWorld())
    {
        const ZoneId focus = WorldDoc.FocusZone();
        for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
        {
            if (zone.Id != focus)
                continue;
            TitleCache += " - ";
            TitleCache += zone.Name;
            break;
        }
    }
    TitleCache += "###Hierarchy";
    return TitleCache;
}

void SceneHierarchyPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    EditorDocument& document = WorldDoc.FocusDocument();
    EditorScene& scene = document.GetScene();
    const SelectableRef current = Selection.GetPrimarySelection();
    const RegistryId registryId = scene.GetRegistry().Id;
    const World& world = scene.GetRegistry().Components;

    // Create a plain entity (Transform only) and select it; the inspector adds
    // game components to it. This is the non-brush authoring path.
    if (ImGui::Button(ICON_FA_PLUS "  New Entity"))
    {
        auto create = MakeCreateEntityCommand(Vec3d::Zero(), scene, document);
        CreateEntityCommand* cmd = create.get();
        Commands.Execute(std::move(create));
        Commands.Execute(std::make_unique<SelectCommand>(
            Selection, SelectableRef::EntitySelection(registryId, cmd->GetCreatedEntity())));
    }
    ImGui::Separator();

    // Deferred so the delete command (which erases from the entity list) does not
    // mutate the vector this loop iterates.
    EntityId toDelete = {};
    ZoneId moveTarget = {};

    for (EntityId entity : scene.GetAllEntities())
    {
        // Registry-driven summary: the components present on this entity, named
        // by the serializer registry — no hard-coded component list.
        std::string summary;
        for (const auto& serializer : EditorSceneSerializers().Entries())
        {
            const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
            if (id == InvalidComponentId || !world.HasComponent(entity, id))
                continue;
            if (!summary.empty())
                summary += ", ";
            summary += std::string(serializer->JsonKey());
        }

        const std::string kind = summary.empty() ? std::string("Entity") : summary;
        // Leading glyph: a populated entity gets a cube, an empty one a faint dot.
        const char* icon = summary.empty() ? ICON_FA_CIRCLE_DOT : ICON_FA_CUBE;
        std::string label = std::string(icon) + "  " + kind + " " + std::to_string(entity.Index);
        label += "##row";

        ImGui::PushID(static_cast<int>(entity.Index));

        // Per-row visibility / lock toggles (editor view flags on the scene).
        const bool visible = scene.IsEntityVisible(entity);
        const bool locked = scene.IsEntityLocked(entity);
        if (ImGui::SmallButton(visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH))
            scene.SetEntityVisible(entity, !visible);
        ImGui::SameLine();
        if (ImGui::SmallButton(locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN))
            scene.SetEntityLocked(entity, !locked);
        ImGui::SameLine();

        // Hidden rows read dimmed; locked rows still select (lock only blocks
        // viewport picking).
        const bool isSelected = current.IsValid() && current.Entity == entity;
        if (!visible)
            ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
        const bool clicked = ImGui::Selectable(label.c_str(), isSelected);
        if (!visible)
            ImGui::PopStyleColor();
        if (clicked)
        {
            Commands.Execute(std::make_unique<SelectCommand>(
                Selection,
                SelectableRef::EntitySelection(registryId, entity)));
        }

        if (ImGui::BeginPopupContextItem("##row_ctx"))
        {
            if (ImGui::MenuItem(ICON_FA_TRASH "  Delete"))
                toDelete = entity;
            if (WorldDoc.IsWorld())
            {
                // Targets: every open zone except the focus zone, manifest order.
                bool hasTarget = false;
                for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
                    if (zone.Id != WorldDoc.FocusZone() && WorldDoc.IsZoneOpen(zone.Id))
                        hasTarget = true;
                if (ImGui::BeginMenu(ICON_FA_ARROW_RIGHT "  Move To Zone", hasTarget))
                {
                    for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
                    {
                        if (zone.Id == WorldDoc.FocusZone() || !WorldDoc.IsZoneOpen(zone.Id))
                            continue;
                        if (ImGui::MenuItem(zone.Name.c_str()))
                            moveTarget = zone.Id;
                    }
                    ImGui::EndMenu();
                }
                if (!hasTarget && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("load a target zone as context first");
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    if (toDelete.IsValid())
        Commands.Execute(MakeDeleteEntitiesCommand(
            std::span<const EntityId>(&toDelete, 1), scene, document, Selection));

    if (moveTarget.IsValid())
    {
        // The move routes the current entity selection, matching the panel
        // acting on selection state rather than the hovered row alone.
        std::vector<EntityId> selectedEntities;
        for (const SelectableRef& ref : Selection.GetSelection())
            if (ref.IsEntity())
                selectedEntities.push_back(ref.Entity);
        if (auto command = MakeMoveEntitiesToZoneCommand(selectedEntities, WorldDoc,
                                                         moveTarget, Selection))
        {
            const size_t count = selectedEntities.size();
            Commands.Execute(std::move(command));
            for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
                if (zone.Id == moveTarget)
                    WorldDoc.Logging().GetLogger<SceneHierarchyPanel>().Info(
                        "moved {} entities to {}", count, zone.Name);
        }
    }

}
