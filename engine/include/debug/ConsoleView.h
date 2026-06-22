#pragma once

#include <core/console/ConsoleHistory.h>
#include <core/console/ConsoleTypes.h>

#include <imgui.h>

#include <array>
#include <string>
#include <vector>

class ConsoleService;
class DebugLogSink;

// Per-host styling for the shared console body. Each host supplies its own colors
// (editor palette vs runtime defaults); the mono font is optional (nullptr falls
// back to the default font) and the Copy button is opt-in.
struct ConsoleViewStyle
{
    std::array<ImVec4, 5> LevelColors{}; // indexed by LogLevel: Debug..Critical
    ImVec4 TextPrimary{ 1.0f, 1.0f, 1.0f, 1.0f };
    ImVec4 Warning{ 1.0f, 0.85f, 0.20f, 1.0f };
    ImVec4 Error{ 1.0f, 0.35f, 0.35f, 1.0f };
    ImFont* Mono = nullptr;
    bool ShowCopy = false;
};

// Persistent per-host state for the shared console body. Holds no ImGui handles,
// only plain data plus a ConsoleHistory, so the input line, filters, transcript,
// and autosuggest selection survive across frames.
struct ConsoleViewState
{
    std::array<bool, 5> LevelFilter{ true, true, true, true, true };
    char CategoryFilter[128] = {};
    std::vector<ConsoleOutputEntry> Transcript;
    bool ScrollToBottom = true;

    char Input[256] = {};
    ConsoleHistory History;
    std::vector<std::string> Suggestions;
    int SelectedSuggestion = -1;
    bool RefocusInput = false;
    bool Initialized = false; // history capacity read from cvar on first draw
};

// Draws the full console body: toolbar (Clear, optional Copy, level filters,
// category filter), an output region (log feed then command transcript), an
// autosuggest list, and the input box (history recall + completion). The caller
// owns the surrounding window (ImGui::Begin / ScopedPanel) and the state/style.
void DrawConsoleView(DebugLogSink& sink,
                     ConsoleService& console,
                     ConsoleViewState& state,
                     const ConsoleViewStyle& style);
