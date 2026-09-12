#include "PanelVisibilitySettings.h"

#include <imgui.h>
#include <imgui_internal.h> // ImGuiSettingsHandler, AddSettingsHandler, MarkIniSettingsDirty

#include <cstdio>

namespace
{
constexpr const char* kSectionType = "EditorPanels";

bool Remembered(const IEditorPanel& panel)
{
    return panel.GetPersistence().Visibility == PanelVisibilityPolicy::Remembered;
}
}

void PanelVisibilitySettings::Register(const std::vector<std::unique_ptr<IEditorPanel>>& panels)
{
    Panels = &panels;
    ImGuiSettingsHandler handler;
    handler.TypeName = kSectionType;
    handler.TypeHash = ImHashStr(kSectionType);
    handler.ReadOpenFn = &ReadOpen;
    handler.ReadLineFn = &ReadLine;
    handler.WriteAllFn = &WriteAll;
    handler.UserData = this;
    ImGui::AddSettingsHandler(&handler);
}

void PanelVisibilitySettings::Apply()
{
    if (Panels == nullptr)
        return;
    Seen.clear();
    for (const std::unique_ptr<IEditorPanel>& panel : *Panels)
    {
        if (panel == nullptr || !Remembered(*panel))
            continue;
        const std::string id(panel->GetPersistence().Id);
        if (const auto it = Recorded.find(id); it != Recorded.end())
            panel->SetVisible(it->second);
        Seen[id] = panel->IsVisible();
    }
}

void PanelVisibilitySettings::Track()
{
    if (Panels == nullptr)
        return;
    bool changed = false;
    for (const std::unique_ptr<IEditorPanel>& panel : *Panels)
    {
        if (panel == nullptr || !Remembered(*panel))
            continue;
        const std::string id(panel->GetPersistence().Id);
        const auto it = Seen.find(id);
        if (it == Seen.end() || it->second != panel->IsVisible())
        {
            Seen[id] = panel->IsVisible();
            changed = true;
        }
    }
    if (changed)
        ImGui::MarkIniSettingsDirty();
}

void* PanelVisibilitySettings::ReadOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name)
{
    auto* self = static_cast<PanelVisibilitySettings*>(handler->UserData);
    // The entry is the map node itself; a node's address is stable for the
    // life of the map.
    return &*self->Recorded.try_emplace(name, true).first;
}

void PanelVisibilitySettings::ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
{
    auto* record = static_cast<std::pair<const std::string, bool>*>(entry);
    int visible = 1;
    if (std::sscanf(line, "Visible=%d", &visible) == 1)
        record->second = visible != 0;
}

void PanelVisibilitySettings::WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out)
{
    auto* self = static_cast<PanelVisibilitySettings*>(handler->UserData);
    if (self->Panels == nullptr)
        return;
    for (const std::unique_ptr<IEditorPanel>& panel : *self->Panels)
    {
        if (panel == nullptr || !Remembered(*panel))
            continue;
        const PanelPersistence persistence = panel->GetPersistence();
        out->appendf("[%s][%.*s]\n", kSectionType, static_cast<int>(persistence.Id.size()), persistence.Id.data());
        out->appendf("Visible=%d\n\n", panel->IsVisible() ? 1 : 0);
    }
}
