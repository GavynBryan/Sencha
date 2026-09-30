#include "PanelVisibilitySettings.h"

#include <imgui.h>
#include <imgui_internal.h> // ImGuiSettingsHandler, AddSettingsHandler, MarkIniSettingsDirty

#include <algorithm>
#include <cstdio>

namespace
{
constexpr const char* kSectionType = "EditorPanels";

bool Remembered(const IEditorPanel& panel)
{
    return panel.GetPersistence().Visibility == PanelVisibilityPolicy::Remembered;
}

std::string ScopedId(std::string_view scope, std::string_view id)
{
    if (scope.empty())
        return std::string(id);
    std::string key;
    key.reserve(scope.size() + 1 + id.size());
    key.append(scope).append("/").append(id);
    return key;
}
}

void PanelVisibilitySettings::Register()
{
    ImGuiSettingsHandler handler;
    handler.TypeName = kSectionType;
    handler.TypeHash = ImHashStr(kSectionType);
    handler.ReadOpenFn = &ReadOpen;
    handler.ReadLineFn = &ReadLine;
    handler.WriteAllFn = &WriteAll;
    handler.UserData = this;
    ImGui::AddSettingsHandler(&handler);
}

void PanelVisibilitySettings::Attach(std::string scope, const std::vector<std::unique_ptr<IEditorPanel>>& panels)
{
    Groups.push_back(Group{ std::move(scope), &panels });
    if (Applied)
        ApplyTo(Groups.back());
}

void PanelVisibilitySettings::Detach(const std::vector<std::unique_ptr<IEditorPanel>>& panels)
{
    std::erase_if(Groups, [&](const Group& group) { return group.Panels == &panels; });
}

void PanelVisibilitySettings::Apply()
{
    Applied = true;
    for (const Group& group : Groups)
        ApplyTo(group);
}

void PanelVisibilitySettings::ApplyTo(const Group& group)
{
    for (const std::unique_ptr<IEditorPanel>& panel : *group.Panels)
    {
        if (panel == nullptr || !Remembered(*panel))
            continue;
        const std::string key = ScopedId(group.Scope, panel->GetPersistence().Id);
        if (const auto it = Recorded.find(key); it != Recorded.end())
            panel->SetVisible(it->second);
        else
            Recorded.emplace(key, panel->IsVisible());
    }
}

void PanelVisibilitySettings::Track()
{
    if (!Applied)
        return;
    bool changed = false;
    for (const Group& group : Groups)
    {
        for (const std::unique_ptr<IEditorPanel>& panel : *group.Panels)
        {
            if (panel == nullptr || !Remembered(*panel))
                continue;
            const std::string key = ScopedId(group.Scope, panel->GetPersistence().Id);
            const auto [it, inserted] = Recorded.try_emplace(key, panel->IsVisible());
            if (inserted || it->second != panel->IsVisible())
            {
                it->second = panel->IsVisible();
                changed = true;
            }
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
    for (const Group& group : self->Groups)
        for (const std::unique_ptr<IEditorPanel>& panel : *group.Panels)
            if (panel != nullptr && Remembered(*panel))
                self->Recorded.insert_or_assign(ScopedId(group.Scope, panel->GetPersistence().Id), panel->IsVisible());
    for (const auto& [key, visible] : self->Recorded)
    {
        out->appendf("[%s][%s]\n", kSectionType, key.c_str());
        out->appendf("Visible=%d\n\n", visible ? 1 : 0);
    }
}
