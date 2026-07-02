#include "MaterialInspectorPanel.h"

#include "EditMaterialCommand.h"

#include "commands/CommandStack.h"
#include "ui/ScopedPanel.h"

#include <core/assets/AssetRegistry.h>

#include <imgui.h>

#include <cfloat>
#include <memory>

MaterialInspectorPanel::MaterialInspectorPanel(MaterialEditSession& session,
                                               CommandStack& commands,
                                               const AssetRegistry& registry)
    : Session(session)
    , Commands(commands)
    , Registry(registry)
{
}

void MaterialInspectorPanel::CommitWidgetEdit(MaterialDescription& edited)
{
    if (ImGui::IsItemActivated())
    {
        EditBaseline = Session.Working();
        BaselineCaptured = true;
    }

    // Live-apply during the drag so the preview tracks the widget.
    if (!SameMaterialDescription(edited, Session.Working()))
        Session.SetWorking(edited);

    if (ImGui::IsItemDeactivatedAfterEdit() && BaselineCaptured)
    {
        if (!SameMaterialDescription(EditBaseline, Session.Working()))
            Commands.Execute(std::make_unique<EditMaterialCommand>(
                Session, EditBaseline, Session.Working()));
        BaselineCaptured = false;
    }
}

void MaterialInspectorPanel::DrawTextureSlot(const char* id, const char* label, AssetRef& slot,
                                             MaterialDescription& edited)
{
    ImGui::PushID(id);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(140.0f);

    const char* current = slot.Path.empty() ? "(none)" : slot.Path.c_str();
    if (ImGui::Button(current, ImVec2(-FLT_MIN, 0.0f)))
        ImGui::OpenPopup("texture_picker");

    if (ImGui::BeginPopup("texture_picker"))
    {
        // "Set" edits happen inside a popup, not a drag, so commit directly:
        // snapshot, apply, push the command in one step.
        const auto apply = [&](const std::string& path)
        {
            const MaterialDescription before = Session.Working();
            slot.Type = AssetType::Texture;
            slot.Path = path;
            Session.SetWorking(edited);
            if (!SameMaterialDescription(before, Session.Working()))
                Commands.Execute(std::make_unique<EditMaterialCommand>(
                    Session, before, Session.Working()));
        };

        if (ImGui::Selectable("(none)"))
            apply(std::string{});
        ImGui::Separator();
        for (const auto& [path, record] : Registry.Records())
        {
            if (record.Type != AssetType::Texture)
                continue;
            if (ImGui::Selectable(record.Path.c_str()))
                apply(record.Path);
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void MaterialInspectorPanel::OnDraw()
{
    if (!IsVisible())
        return;

    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    if (!Session.HasOpen())
    {
        ImGui::TextDisabled("Open a material from the Materials panel.");
        return;
    }

    ImGui::TextUnformatted(Session.VirtualPath().c_str());
    if (Session.IsDirty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(modified)");
    }
    ImGui::Separator();

    MaterialDescription edited = Session.Working();

    if (ImGui::CollapsingHeader("Base Color", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float color[4] = { edited.BaseColorFactor.X, edited.BaseColorFactor.Y,
                           edited.BaseColorFactor.Z, edited.BaseColorFactor.W };
        ImGui::ColorEdit4("Factor", color, ImGuiColorEditFlags_Float);
        edited.BaseColorFactor = Vec4(color[0], color[1], color[2], color[3]);
        CommitWidgetEdit(edited);
        DrawTextureSlot("base_color_texture", "Texture", edited.BaseColorTexture, edited);
    }

    if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Roughness", &edited.RoughnessFactor, 0.0f, 1.0f);
        CommitWidgetEdit(edited);
        ImGui::SliderFloat("Metallic", &edited.MetallicFactor, 0.0f, 1.0f);
        CommitWidgetEdit(edited);
        DrawTextureSlot("orm_texture", "ORM Texture", edited.OrmTexture, edited);
    }

    if (ImGui::CollapsingHeader("Normal", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawTextureSlot("normal_texture", "Texture", edited.NormalTexture, edited);
        ImGui::SliderFloat("Scale", &edited.NormalScale, 0.0f, 2.0f);
        CommitWidgetEdit(edited);
    }

    if (ImGui::CollapsingHeader("Emissive", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float emissive[3] = { edited.EmissiveFactor.X, edited.EmissiveFactor.Y,
                              edited.EmissiveFactor.Z };
        ImGui::ColorEdit3("Factor##emissive", emissive,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        edited.EmissiveFactor = Vec4(emissive[0], emissive[1], emissive[2], 0.0f);
        CommitWidgetEdit(edited);
        DrawTextureSlot("emissive_texture", "Texture", edited.EmissiveTexture, edited);
    }

    if (ImGui::CollapsingHeader("Alpha", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static constexpr const char* kModes[] = { "opaque", "mask", "blend" };
        int mode = static_cast<int>(edited.AlphaMode);
        if (ImGui::Combo("Mode", &mode, kModes, 3))
        {
            const MaterialDescription before = Session.Working();
            edited.AlphaMode = static_cast<MaterialAlphaMode>(mode);
            Session.SetWorking(edited);
            Commands.Execute(std::make_unique<EditMaterialCommand>(
                Session, before, Session.Working()));
        }
        if (edited.AlphaMode == MaterialAlphaMode::Mask)
        {
            ImGui::SliderFloat("Cutoff", &edited.AlphaCutoff, 0.0f, 1.0f);
            CommitWidgetEdit(edited);
        }
    }
}
