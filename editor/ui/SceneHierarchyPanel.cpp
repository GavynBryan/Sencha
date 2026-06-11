#include "SceneHierarchyPanel.h"

#include "../commands/CommandStack.h"
#include "../level/LevelScene.h"
#include "../selection/SelectCommand.h"
#include "../selection/SelectionService.h"

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

    for (EntityId entity : Scene.GetAllEntities())
    {
        const char* kind = "Entity";
        if (Scene.TryGetBrush(entity) != nullptr)
            kind = "Brush";
        else if (Scene.TryGetCamera(entity) != nullptr)
            kind = "Camera";

        std::string label = std::string(kind) + " " + std::to_string(entity.Index);
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
