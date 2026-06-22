#include "SceneHierarchyPanel.h"

#include "EditorUiStyle.h"
#include "ScopedPanel.h"
#include "fonts/IconsFontAwesome6.h"

#include "../commands/CommandStack.h"
#include "../level/commands/CreateEntityCommand.h"
#include "../level/commands/DeleteEntityCommand.h"
#include "../level/LevelScene.h"
#include "../selection/commands/SelectCommand.h"
#include "../selection/SelectionService.h"

#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializer.h>

#include <imgui.h>

#include <memory>
#include <span>
#include <string>

SceneHierarchyPanel::SceneHierarchyPanel(LevelScene& scene, LevelDocument& document,
                                         SelectionService& selection, CommandStack& commands)
    : Scene(scene)
    , Document(document)
    , Selection(selection)
    , Commands(commands)
{
}

std::string_view SceneHierarchyPanel::GetTitle() const
{
    return "Hierarchy";
}

void SceneHierarchyPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    const SelectableRef current = Selection.GetPrimarySelection();
    const RegistryId registryId = Scene.GetRegistry().Id;
    const World& world = Scene.GetRegistry().Components;

    // Create a plain entity (Transform only) and select it; the inspector adds
    // game components to it. This is the non-brush authoring path.
    if (ImGui::Button(ICON_FA_PLUS "  New Entity"))
    {
        auto create = MakeCreateEntityCommand(Vec3d::Zero(), Scene, Document);
        CreateEntityCommand* cmd = create.get();
        Commands.Execute(std::move(create));
        Commands.Execute(std::make_unique<SelectCommand>(
            Selection, SelectableRef::EntitySelection(registryId, cmd->GetCreatedEntity())));
    }
    ImGui::Separator();

    // Deferred so the delete command (which erases from the entity list) does not
    // mutate the vector this loop iterates.
    EntityId toDelete = {};

    for (EntityId entity : Scene.GetAllEntities())
    {
        // Registry-driven summary: the components present on this entity, named
        // by the serializer registry — no hard-coded component list.
        std::string summary;
        for (const auto& serializer : GetComponentSerializerEntries())
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
        const bool visible = Scene.IsEntityVisible(entity);
        const bool locked = Scene.IsEntityLocked(entity);
        if (ImGui::SmallButton(visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH))
            Scene.SetEntityVisible(entity, !visible);
        ImGui::SameLine();
        if (ImGui::SmallButton(locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN))
            Scene.SetEntityLocked(entity, !locked);
        ImGui::SameLine();

        // Hidden rows read dimmed; locked rows still select (lock only blocks
        // viewport picking, matching Hammer).
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
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    if (toDelete.IsValid())
        Commands.Execute(MakeDeleteEntitiesCommand(
            std::span<const EntityId>(&toDelete, 1), Scene, Document, Selection));
}
