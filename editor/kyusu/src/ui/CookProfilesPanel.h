#pragma once

#include "ui/IEditorPanel.h"

#include <string>

struct CookProfile;
struct ProjectDescriptor;

class CookProfilesPanel : public IEditorPanel
{
public:
    explicit CookProfilesPanel(ProjectDescriptor* project);

    std::string_view GetTitle() const override { return "Cook Profiles"; }
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }
    int GetDockTabGroup() const override { return 0; }

private:
    [[nodiscard]] CookProfile* EditableProfile();
    void AddProfile(const CookProfile* source);
    void Save();
    void Validate();

    ProjectDescriptor* Project = nullptr;
    std::string SelectedId = "full";
    std::string Message;
    bool Dirty = false;
};
