#pragma once

#include "IEditorPanel.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiContext;
struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;

// Remembers which panels a user has shown or hidden, in the ImGui layout file,
// per Remembered panel under `<scope>/<settings id>`. What the file recorded for
// a group not attached is kept, so a closed workspace's choices survive.
class PanelVisibilitySettings
{
public:
    // Once the context exists and before its first NewFrame, which reads the file.
    void Register();
    // The list must outlive the attachment. After Apply, it takes its state at once.
    void Attach(std::string scope, const std::vector<std::unique_ptr<IEditorPanel>>& panels);
    void Detach(const std::vector<std::unique_ptr<IEditorPanel>>& panels);
    // Once, after the first frame has read the file.
    void Apply();
    // Every frame: a docked tab's close box writes the flag directly, so there
    // is no toggle to hang a save on.
    void Track();

private:
    struct Group
    {
        std::string Scope;
        const std::vector<std::unique_ptr<IEditorPanel>>* Panels = nullptr;
    };

    void ApplyTo(const Group& group);

    static void* ReadOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name);
    static void ReadLine(ImGuiContext*, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out);

    std::vector<Group> Groups;
    // Every recorded choice by scoped id, attached or not. Ordered so the file
    // writes the same way every time.
    std::map<std::string, bool, std::less<>> Recorded;
    bool Applied = false;
};
