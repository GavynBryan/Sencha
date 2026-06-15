#include "SceneHierarchyPanel.h"

#include "../commands/CommandStack.h"
#include "../level/LevelScene.h"
#include "../selection/commands/SelectCommand.h"
#include "../selection/SelectionService.h"

#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializer.h>

#include <imgui.h>

#include <memory>
#include <string>

SceneHierarchyPanel::SceneHierarchyPanel(const LevelScene& scene, SelectionService& selection, CommandStack& commands)
    : Scene(scene)
    , Selection(selection)
    , Commands(commands)
{
}

std::string_view SceneHierarchyPanel::GetTitle() const
{
    return "Hierarchy";
}

bool SceneHierarchyPanel::IsVisible() const
{
    return Visible;
}

void SceneHierarchyPanel::OnDraw()
{
    if (!ImGui::Begin(GetTitle().data(), &Visible))
    {
        ImGui::End();
        return;
    }

    const SelectableRef current = Selection.GetPrimarySelection();
    const RegistryId registryId = Scene.GetRegistry().Id;
    const World& world = Scene.GetRegistry().Components;

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
        std::string label = kind + " " + std::to_string(entity.Index);
        label += "##" + std::to_string(entity.Index) + "_" + std::to_string(entity.Generation);

        const bool isSelected = current.IsValid() && current.Entity == entity;
        if (ImGui::Selectable(label.c_str(), isSelected))
        {
            Commands.Execute(std::make_unique<SelectCommand>(
                Selection,
                SelectableRef::EntitySelection(registryId, entity)));
        }
    }

    ImGui::End();
}
