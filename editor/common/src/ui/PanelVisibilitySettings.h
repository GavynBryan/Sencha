#pragma once

#include "IEditorPanel.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ImGuiContext;
struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;

// Remembers which panels a user has shown or hidden, in the ImGui layout
// file beside the dock placement it belongs with. One ini section per panel,
// keyed by the panel's declared settings id; only panels whose policy is
// Remembered are written or restored, so a session-only surface keeps the
// startup state its owner gave it.
//
// Lifecycle: Register once the ImGui context exists and before its first
// NewFrame (which is when ImGui reads the file); Apply once after that frame;
// Track every frame, because the close box on a docked tab writes the
// panel's flag directly and there is no toggle to hang a save on.
class PanelVisibilitySettings
{
public:
    // The panel list is the shell's; it outlives this object.
    void Register(const std::vector<std::unique_ptr<IEditorPanel>>& panels);
    // Pushes what the file recorded onto the Remembered panels.
    void Apply();
    // Marks the file dirty when a Remembered panel's visibility changed since
    // the last call.
    void Track();

private:
    static void* ReadOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name);
    static void ReadLine(ImGuiContext*, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out);

    const std::vector<std::unique_ptr<IEditorPanel>>* Panels = nullptr;
    // What the file said, by id. Ids no panel declares any more are dropped on
    // the next save.
    std::unordered_map<std::string, bool> Recorded;
    // Each Remembered panel's visibility as of the last Track, by id.
    std::unordered_map<std::string, bool> Seen;
};
