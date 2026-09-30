#include "MaterialInspectorPanel.h"

#include "EditMaterialCommand.h"

#include "ui/ScopedPanel.h"

#include <core/assets/AssetRegistry.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <memory>
#include <string_view>

namespace
{
    bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty())
            return true;
        const auto it = std::search(
            haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b)
            { return std::tolower(static_cast<unsigned char>(a))
                  == std::tolower(static_cast<unsigned char>(b)); });
        return it != haystack.end();
    }

    // Source textures only. The registry also holds physical cooked artifacts.
    // Materials reference the source path, which the asset system resolves to
    // cooked bytes, so cooked spellings never belong in the picker.
    bool IsPickableTexture(const AssetRecord& record)
    {
        if (record.Type != AssetType::Texture)
            return false;
        constexpr std::string_view cooked = ".stex";
        const std::string_view path = record.Path;
        return path.size() < cooked.size()
            || path.substr(path.size() - cooked.size()) != cooked;
    }
}

MaterialInspectorPanel::MaterialInspectorPanel(
    MaterialTabSet& tabs, const AssetRegistry& registry,
    std::function<void(const std::string&)> openTextureSettings)
    : Tabs(tabs)
    , Registry(registry)
    , OpenTextureSettings(std::move(openTextureSettings))
{
}

void MaterialInspectorPanel::CommitWidgetEdit(MaterialEditTab& tab, MaterialDescription& edited)
{
    if (ImGui::IsItemActivated())
    {
        EditBaseline = tab.Session.Working();
        BaselineCaptured = true;
    }

    if (!SameMaterialDescription(edited, tab.Session.Working()))
        tab.Session.SetWorking(edited);

    if (ImGui::IsItemDeactivatedAfterEdit() && BaselineCaptured)
    {
        if (!SameMaterialDescription(EditBaseline, tab.Session.Working()))
            tab.Commands.Execute(std::make_unique<EditMaterialCommand>(
                tab.Session, EditBaseline, tab.Session.Working()));
        BaselineCaptured = false;
    }
}

void MaterialInspectorPanel::DrawTextureSlot(MaterialEditTab& tab, const char* id,
                                             const char* label, AssetRef& slot,
                                             MaterialDescription& edited)
{
    ImGui::PushID(id);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(140.0f);

    const char* current = slot.Path.empty() ? "(none)" : slot.Path.c_str();
    if (ImGui::Button(current, ImVec2(-FLT_MIN, 0.0f)))
    {
        TextureFilterText[0] = '\0';
        ImGui::OpenPopup("texture_picker");
    }
    if (!slot.Path.empty() && ImGui::BeginPopupContextItem("slot_context"))
    {
        if (ImGui::MenuItem("Import Settings...") && OpenTextureSettings)
            OpenTextureSettings(slot.Path);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("texture_picker"))
    {
        const auto apply = [&](const std::string& path)
        {
            const MaterialDescription before = tab.Session.Working();
            slot.Type = AssetType::Texture;
            slot.Path = path;
            tab.Session.SetWorking(edited);
            if (!SameMaterialDescription(before, tab.Session.Working()))
                tab.Commands.Execute(std::make_unique<EditMaterialCommand>(
                    tab.Session, before, tab.Session.Working()));
            ImGui::CloseCurrentPopup();
        };

        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("##texfilter", "filter textures", TextureFilterText,
                                 sizeof(TextureFilterText));

        if (ImGui::Selectable("(none)"))
            apply(std::string{});
        ImGui::Separator();

        if (ImGui::BeginChild("##texlist", ImVec2(320.0f, 300.0f)))
        {
            const std::string_view filter = TextureFilterText;
            int shown = 0;
            for (const auto& [path, record] : Registry.Records())
            {
                (void)path;
                if (!IsPickableTexture(record))
                    continue;
                if (!ContainsCaseInsensitive(record.Path, filter))
                    continue;
                ++shown;
                ImGui::PushID(record.Path.c_str());
                if (ImGui::Selectable(record.Path.c_str(), record.Path == slot.Path))
                    apply(record.Path);
                if (ImGui::BeginPopupContextItem("row_context"))
                {
                    if (ImGui::MenuItem("Import Settings..."))
                    {
                        if (OpenTextureSettings)
                            OpenTextureSettings(record.Path);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            if (shown == 0)
                ImGui::TextDisabled("no textures match");
        }
        ImGui::EndChild();
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

    MaterialEditTab* tab = Tabs.Active();
    if (tab == nullptr || !tab->Session.HasOpen())
    {
        ImGui::TextDisabled("Open a material from the Materials panel.");
        return;
    }

    ImGui::TextUnformatted(tab->Session.VirtualPath().c_str());
    if (tab->Session.IsDirty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(modified)");
    }
    ImGui::Separator();

    MaterialDescription edited = tab->Session.Working();
    const auto commitDiscrete = [&](const MaterialDescription& value)
    {
        const MaterialDescription before = tab->Session.Working();
        tab->Session.SetWorking(value);
        if (!SameMaterialDescription(before, tab->Session.Working()))
            tab->Commands.Execute(std::make_unique<EditMaterialCommand>(
                tab->Session, before, tab->Session.Working()));
    };

    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static constexpr const char* kShadingNames[] = { "Standard Lit", "Unlit" };
        int shading = static_cast<int>(edited.Shading);
        if (ImGui::Combo("Shading", &shading, kShadingNames, 2))
        {
            edited.Shading = static_cast<MaterialShading>(shading);
            commitDiscrete(edited);
        }

        if (ImGui::Checkbox("Double Sided", &edited.DoubleSided))
            commitDiscrete(edited);
        if (ImGui::Checkbox("Cast Shadows", &edited.CastShadows))
            commitDiscrete(edited);
        if (ImGui::Checkbox("Receive Shadows", &edited.ReceiveShadows))
            commitDiscrete(edited);
    }

    if (ImGui::CollapsingHeader("Base Color", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float color[4] = { edited.BaseColorFactor.X, edited.BaseColorFactor.Y,
                           edited.BaseColorFactor.Z, edited.BaseColorFactor.W };
        ImGui::ColorEdit4("Factor", color, ImGuiColorEditFlags_Float);
        edited.BaseColorFactor = Vec4(color[0], color[1], color[2], color[3]);
        CommitWidgetEdit(*tab, edited);
        DrawTextureSlot(*tab, "base_color_texture", "Texture", edited.BaseColorTexture, edited);
    }

    if (edited.Shading == MaterialShading::StandardLit
        && ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Roughness", &edited.RoughnessFactor, 0.0f, 1.0f);
        CommitWidgetEdit(*tab, edited);
        ImGui::SliderFloat("Metallic", &edited.MetallicFactor, 0.0f, 1.0f);
        CommitWidgetEdit(*tab, edited);
        ImGui::SliderFloat("Specular", &edited.SpecularIntensity, 0.0f, 1.0f);
        CommitWidgetEdit(*tab, edited);
        DrawTextureSlot(*tab, "orm_texture", "ORM Texture", edited.OrmTexture, edited);
    }

    if (edited.Shading == MaterialShading::StandardLit
        && ImGui::CollapsingHeader("Normal", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawTextureSlot(*tab, "normal_texture", "Texture", edited.NormalTexture, edited);
        ImGui::SliderFloat("Scale", &edited.NormalScale, 0.0f, 2.0f);
        CommitWidgetEdit(*tab, edited);
    }

    if (ImGui::CollapsingHeader("Emissive", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float emissive[3] = { edited.EmissiveFactor.X, edited.EmissiveFactor.Y,
                              edited.EmissiveFactor.Z };
        ImGui::ColorEdit3("Factor##emissive", emissive,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        edited.EmissiveFactor = Vec4(emissive[0], emissive[1], emissive[2], 0.0f);
        CommitWidgetEdit(*tab, edited);
        ImGui::DragFloat("Strength", &edited.EmissiveStrength, 0.05f, 0.0f, 1000.0f);
        CommitWidgetEdit(*tab, edited);
        DrawTextureSlot(*tab, "emissive_texture", "Texture", edited.EmissiveTexture, edited);
    }

    if (ImGui::CollapsingHeader("Alpha", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static constexpr const char* kModes[] = { "opaque", "mask", "blend" };
        int mode = static_cast<int>(edited.AlphaMode);
        if (ImGui::Combo("Mode", &mode, kModes, 3))
        {
            edited.AlphaMode = static_cast<MaterialAlphaMode>(mode);
            commitDiscrete(edited);
        }
        if (edited.AlphaMode == MaterialAlphaMode::Mask)
        {
            ImGui::SliderFloat("Cutoff", &edited.AlphaCutoff, 0.0f, 1.0f);
            CommitWidgetEdit(*tab, edited);
        }
    }
}
