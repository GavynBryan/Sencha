#include "InspectorPanel.h"

#include "SchemaWidgets.h"

#include "../commands/CommandStack.h"
#include "../level/LevelCommands.h"
#include "../level/LevelDocument.h"
#include "../selection/SelectionService.h"

#include <imgui.h>

#include <memory>

InspectorPanel::InspectorPanel(LevelScene& scene,
                               LevelDocument& document,
                               SelectionService& selection,
                               CommandStack& commands)
    : Scene(scene)
    , Document(document)
    , Selection(selection)
    , Commands(commands)
{
}

std::string_view InspectorPanel::GetTitle() const
{
    return "Inspector";
}

bool InspectorPanel::IsVisible() const
{
    return Visible;
}

void InspectorPanel::ResetEditState()
{
    TransformEdit.Reset();
    BrushEdit.Reset();
    CameraEdit.Reset();
}

template <typename T>
void InspectorPanel::DrawComponentSection(const char* label, EntityId entity, const T* current,
                                          ComponentEditState<T>& state)
{
    if (current == nullptr)
    {
        state.Reset();
        return;
    }

    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::PushID(label);

    // While a drag is in progress, keep showing the working copy so the edit
    // is not clobbered by the unchanged scene value.
    T value = state.Working.has_value() ? *state.Working : *current;

    const SchemaWidgetResult result = DrawSchemaFields(value);

    if (result.Changed)
    {
        if (!state.Original.has_value())
            state.Original = *current;
        state.Working = value;
    }

    if (result.Committed)
    {
        if (state.Original.has_value())
        {
            Commands.Execute(std::make_unique<EditComponentCommand<T>>(
                entity, *state.Original, value, Scene, Document));
        }
        state.Reset();
    }

    ImGui::PopID();
}

void InspectorPanel::OnDraw()
{
    if (!ImGui::Begin(GetTitle().data(), &Visible))
    {
        ImGui::End();
        return;
    }

    const SelectableRef selection = Selection.GetPrimarySelection();
    const EntityId entity = selection.Entity;

    if (!selection.IsValid() || !Scene.HasEntity(entity))
    {
        ImGui::TextDisabled("No selection");
        ResetEditState();
        LastEntity = {};
        ImGui::End();
        return;
    }

    if (!(entity == LastEntity))
    {
        ResetEditState();
        LastEntity = entity;
    }

    ImGui::Text("Entity %u (gen %u)", entity.Index, entity.Generation);
    ImGui::Separator();

    std::optional<LocalTransform> transform;
    if (const Transform3f* current = Scene.TryGetTransform(entity))
        transform = LocalTransform{ *current };

    DrawComponentSection("Transform", entity, transform.has_value() ? &*transform : nullptr, TransformEdit);
    DrawComponentSection("Brush", entity, Scene.TryGetBrush(entity), BrushEdit);
    DrawComponentSection("Camera", entity, Scene.TryGetCamera(entity), CameraEdit);

    ImGui::End();
}
