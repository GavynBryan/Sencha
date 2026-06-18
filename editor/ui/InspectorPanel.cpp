#include "InspectorPanel.h"

#include "EditorUiSkin.h"
#include "fonts/IconsFontAwesome6.h"

#include "../commands/CommandStack.h"
#include "../level/commands/RawComponentEditCommand.h"
#include "../level/commands/RawComponentAddCommand.h"
#include "../level/LevelDocument.h"
#include "../selection/SelectionService.h"

#include <core/metadata/RuntimeSchema.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializer.h>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace
{
    ImGuiDataType DataTypeFor(const RuntimeField& field)
    {
        switch (field.Scalar)
        {
        case FieldScalar::Float:  return ImGuiDataType_Float;
        case FieldScalar::Double: return ImGuiDataType_Double;
        case FieldScalar::Int32:
            return field.Size == 1 ? ImGuiDataType_S8
                 : field.Size == 2 ? ImGuiDataType_S16
                 : field.Size >= 8 ? ImGuiDataType_S64
                                   : ImGuiDataType_S32;
        case FieldScalar::UInt32:
            return field.Size == 1 ? ImGuiDataType_U8
                 : field.Size == 2 ? ImGuiDataType_U16
                 : field.Size >= 8 ? ImGuiDataType_U64
                                   : ImGuiDataType_U32;
        default:
            return ImGuiDataType_S32;
        }
    }

    struct FieldEdit
    {
        bool Activated = false; // widget gained focus this frame (pre-edit)
        bool Committed = false; // edit finished this frame
    };

    // Lays out one row as [label column | widget column], the widget filling the
    // rest of the width — the two-column inspector look. The widget uses a hidden
    // ("##") label so only the left-column text shows.
    FieldEdit DrawRuntimeField(const RuntimeField& field, std::byte* component, float labelWidth)
    {
        FieldEdit edit;
        void* ptr = component + field.Offset;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(field.Name.c_str());
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);

        const std::string id = "##" + field.Name;
        if (field.Scalar == FieldScalar::Unsupported)
        {
            ImGui::TextDisabled("<unsupported>");
            return edit;
        }

        if (field.Scalar == FieldScalar::Bool)
            ImGui::Checkbox(id.c_str(), reinterpret_cast<bool*>(ptr));
        else
            ImGui::DragScalar(id.c_str(), DataTypeFor(field), ptr, 0.05f);

        edit.Activated = ImGui::IsItemActivated();
        edit.Committed = ImGui::IsItemDeactivatedAfterEdit();
        return edit;
    }
}

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

void InspectorPanel::ResetEditState()
{
    EditActive = false;
    EditingComponent = InvalidComponentId;
    EditBefore.clear();
}

void InspectorPanel::DrawComponent(const IComponentSerializer& serializer, EntityId entity)
{
    World& world = Scene.GetRegistry().Components;
    const ComponentId id = world.GetComponentIdByType(serializer.TypeId());
    if (id == InvalidComponentId)
        return;

    const ComponentMeta* meta = world.GetMeta(id);
    const std::size_t size = meta ? meta->Size : 0;

    const std::string key(serializer.JsonKey());
    // Stable id from the JsonKey; the icon is display-only (kept out of the id).
    const std::string header = std::string(ICON_FA_CUBES "  ") + key + "###" + key;
    if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::PushID(key.c_str());

    // Work on a copy of the component's bytes so reads don't churn change
    // tracking every frame; write back only when a widget actually edits.
    std::vector<std::byte> working(size);
    if (size > 0)
    {
        const void* live = world.GetComponentRaw(entity, id);
        if (live == nullptr)
        {
            ImGui::PopID();
            return;
        }
        std::memcpy(working.data(), live, size);
    }
    const std::vector<std::byte> frameStart = working;

    // Label column ~42% of the section width, clamped, so widgets line up.
    const float labelWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.42f, 70.0f, 200.0f);

    bool activated = false;
    bool committed = false;
    for (const RuntimeField& field : serializer.RuntimeFields())
    {
        const FieldEdit edit = DrawRuntimeField(field, working.data(), labelWidth);
        activated |= edit.Activated;
        committed |= edit.Committed;
    }

    // Begin an undoable edit: snapshot the pre-edit bytes on widget activation.
    // Refresh on every activation (only one widget is active at a time) so a
    // click-release without an edit can't leave a stale snapshot behind.
    if (activated)
    {
        EditActive = true;
        EditingComponent = id;
        EditBefore = frameStart;
    }

    // Apply this frame's edit to the live component for immediate feedback.
    if (size > 0 && std::memcmp(working.data(), frameStart.data(), size) != 0)
    {
        if (void* live = world.GetComponentRaw(entity, id))
            std::memcpy(live, working.data(), size);
    }

    // Commit: record one undoable command spanning the whole drag.
    if (committed && EditActive && EditingComponent == id)
    {
        Commands.Execute(std::make_unique<RawComponentEditCommand>(
            entity, id, EditBefore, working, Scene, Document));
        EditActive = false;
        EditingComponent = InvalidComponentId;
    }

    ImGui::PopID();
}

void InspectorPanel::DrawAddComponentMenu(EntityId entity)
{
    World& world = Scene.GetRegistry().Components;

    // OpenPopup only sets state; BeginPopup must run every frame or ImGui closes
    // the popup before a selection can be made.
    if (ImGui::Button(ICON_FA_PLUS "  Add Component"))
        ImGui::OpenPopup("##add_component");

    if (ImGui::BeginPopup("##add_component"))
    {
        bool anyAddable = false;
        for (const auto& serializer : GetComponentSerializerEntries())
        {
            const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
            if (id == InvalidComponentId || world.HasComponent(entity, id))
                continue;

            anyAddable = true;
            const std::string label(serializer->JsonKey());
            if (ImGui::Selectable(label.c_str()))
            {
                Commands.Execute(std::make_unique<RawComponentAddCommand>(
                    entity, id, serializer->DefaultBytes(), Scene, Document));
            }
        }

        if (!anyAddable)
            ImGui::TextDisabled("All components present");

        ImGui::EndPopup();
    }
}

void InspectorPanel::OnDraw()
{
    if (!ImGui::Begin(GetTitle().data(), &Visible))
    {
        ImGui::End();
        return;
    }
    EditorUiSkin::PanelBackdrop();

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

    ImGui::AlignTextToFramePadding();
    ImGui::Text(ICON_FA_CUBE "  Entity %u", entity.Index);
    ImGui::SameLine();
    ImGui::TextDisabled("(gen %u)", entity.Generation);
    ImGui::Separator();

    // Registry-driven: every component the registry knows about, drawn by schema.
    // No component is named in editor code here.
    World& world = Scene.GetRegistry().Components;
    for (const auto& serializer : GetComponentSerializerEntries())
    {
        const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
        if (id != InvalidComponentId && world.HasComponent(entity, id))
            DrawComponent(*serializer, entity);
    }

    ImGui::Separator();
    DrawAddComponentMenu(entity);

    ImGui::End();
}
