#include "MaterialBrowserPanel.h"

#include "MaterialEditSession.h"

#include "project/MaterialLibrary.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <cfloat>

MaterialBrowserPanel::MaterialBrowserPanel(MaterialLibrary& materials,
                                           const MaterialEditSession& session,
                                           Actions actions)
    : Materials(materials)
    , Session(session)
    , Act(std::move(actions))
{
}

void MaterialBrowserPanel::OnDraw()
{
    if (!IsVisible())
        return;

    ScopedPanel panel(GetTitle().data(), &Visible);
    if (!panel.IsOpen())
        return;

    if (ImGui::Button("Rescan") && Act.Rescan)
        Act.Rescan();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu material(s)", Materials.Materials().size());

    ImGui::Separator();
    ImGui::InputText("##name", NameBuffer, sizeof(NameBuffer));
    ImGui::SameLine();
    const bool nameValid = NameBuffer[0] != '\0';
    ImGui::BeginDisabled(!nameValid);
    if (ImGui::Button("New") && Act.CreateNew)
        Act.CreateNew(NameBuffer);
    ImGui::SameLine();
    ImGui::BeginDisabled(!Session.HasOpen());
    if (ImGui::Button("Duplicate") && Act.Duplicate)
        Act.Duplicate(NameBuffer);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("New writes a default material; Duplicate copies the open one. Both land in materials/<name>.smat under the first content root.");

    ImGui::Separator();
    if (ImGui::BeginListBox("##materials", ImVec2(-FLT_MIN, -FLT_MIN)))
    {
        for (const MaterialAsset& material : Materials.Materials())
        {
            const bool selected = Session.HasOpen() && Session.VirtualPath() == material.Path;
            if (ImGui::Selectable(material.DisplayName.c_str(), selected) && Act.Open)
                Act.Open(material.Path);
        }
        ImGui::EndListBox();
    }
}
