#include "EditorConsolePanel.h"

#include "EditorUiStyle.h"
#include "ScopedPanel.h"

#include <core/console/ConsoleService.h>
#include <debug/DebugLogEntry.h>
#include <debug/DebugLogSink.h>

#include <imgui.h>

namespace
{
    ImVec4 LevelColour(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug:    return EditorUi::TextDim;
        case LogLevel::Info:     return EditorUi::TextPrimary;
        case LogLevel::Warning:  return EditorUi::Warning;
        case LogLevel::Error:    return EditorUi::Danger;
        case LogLevel::Critical: return EditorUi::Critical;
        default:                 return EditorUi::TextPrimary;
        }
    }
}

EditorConsolePanel::EditorConsolePanel(DebugLogSink& sink, ConsoleService& console)
    : Sink(sink)
    , Console(console)
{
}

std::string_view EditorConsolePanel::GetTitle() const
{
    return "Console";
}

void EditorConsolePanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    if (ImGui::Button("Clear"))
    {
        Sink.Clear();
        CommandOutput.clear();
    }
    ImGui::SameLine();
    // ImGui text isn't selectable; copy the visible (filtered) transcript to the
    // clipboard instead. LogToClipboard captures whatever is rendered between it
    // and LogFinish, so it honors the level/category filters automatically.
    const bool copyRequested = ImGui::Button("Copy");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy the visible log to the clipboard");
    ImGui::SameLine();
    static constexpr const char* LevelNames[] = { "Debug", "Info", "Warn", "Error", "Crit" };
    for (int i = 0; i < 5; ++i)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColour(static_cast<LogLevel>(i)));
        ImGui::Checkbox(LevelNames[i], &LevelFilter[static_cast<std::size_t>(i)]);
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("Category", CategoryFilterBuf, sizeof(CategoryFilterBuf));

    ImGui::Separator();

    // Output region, sized to leave the command input pinned at the bottom (the
    // input is drawn after this child — terminal layout, newest output above it).
    const float footerHeight = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("ConsoleScroll", ImVec2(0.0f, -footerHeight), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    // Sticky auto-scroll: follow the tail while the user is already pinned to the
    // bottom (measured before this frame's content), or right after a command. Both
    // streams render oldest->newest, so the most recent line sits at the bottom.
    const bool stick = ScrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
    ScrollToBottom = false;

    if (ImFont* mono = EditorUi::MonoFont())
        ImGui::PushFont(mono);

    if (copyRequested)
        ImGui::LogToClipboard();

    // Engine log feed first (chronological); the user's command transcript renders
    // below it so it stays just above the input box.
    const std::string_view categoryFilter(CategoryFilterBuf);
    for (std::size_t i = 0; i < Sink.Count(); ++i)
    {
        const DebugLogEntry& e = Sink.GetEntry(i);
        const int levelIdx = static_cast<int>(e.Level);
        if (levelIdx < 0 || levelIdx >= 5)
            continue;
        if (!LevelFilter[static_cast<std::size_t>(levelIdx)])
            continue;
        if (!categoryFilter.empty() && e.Category.find(categoryFilter) == std::string::npos)
            continue;
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColour(e.Level));
        ImGui::Text("[%s] %s", e.Category.c_str(), e.Message.c_str());
        ImGui::PopStyleColor();
    }

    for (const ConsoleOutputEntry& entry : CommandOutput)
    {
        ImVec4 color = EditorUi::TextPrimary;
        if (entry.Severity == ConsoleOutputSeverity::Warning)
            color = EditorUi::Warning;
        else if (entry.Severity == ConsoleOutputSeverity::Error)
            color = EditorUi::Danger;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(entry.Text.c_str());
        ImGui::PopStyleColor();
    }

    if (copyRequested)
        ImGui::LogFinish();

    if (EditorUi::MonoFont())
        ImGui::PopFont();
    if (stick)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // Command input, pinned at the bottom.
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##editor_console_command", CommandBuf, sizeof(CommandBuf),
                         ImGuiInputTextFlags_EnterReturnsTrue)
        && CommandBuf[0] != '\0')
    {
        CommandOutput.push_back({
            .Severity = ConsoleOutputSeverity::Info,
            .Channel = "input",
            .Text = std::string("> ") + CommandBuf,
        });
        ConsoleResult result =
            Console.ExecuteLine(CommandBuf, { .Description = "editor console ui" }, false);
        CommandOutput.insert(CommandOutput.end(), result.Output.begin(), result.Output.end());
        CommandBuf[0] = '\0';
        ScrollToBottom = true;
        ImGui::SetKeyboardFocusHere(-1);
    }
}
