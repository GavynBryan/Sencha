#include "EditorConsolePanel.h"

#include <core/console/ConsoleService.h>
#include <debug/DebugLogEntry.h>
#include <debug/DebugLogSink.h>

#include <imgui.h>

namespace
{
    constexpr ImVec4 LevelColour(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug:    return { 0.60f, 0.60f, 0.60f, 1.0f };
        case LogLevel::Info:     return { 1.00f, 1.00f, 1.00f, 1.0f };
        case LogLevel::Warning:  return { 1.00f, 0.85f, 0.20f, 1.0f };
        case LogLevel::Error:    return { 1.00f, 0.35f, 0.35f, 1.0f };
        case LogLevel::Critical: return { 1.00f, 0.20f, 0.80f, 1.0f };
        default:                 return { 1.00f, 1.00f, 1.00f, 1.0f };
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

bool EditorConsolePanel::IsVisible() const
{
    return Visible;
}

void EditorConsolePanel::OnDraw()
{
    if (!ImGui::Begin(GetTitle().data(), &Visible))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear"))
    {
        Sink.Clear();
        CommandOutput.clear();
    }
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
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const ConsoleOutputEntry& entry : CommandOutput)
    {
        ImVec4 color = { 0.80f, 0.80f, 0.80f, 1.0f };
        if (entry.Severity == ConsoleOutputSeverity::Warning)
            color = { 1.00f, 0.85f, 0.20f, 1.0f };
        else if (entry.Severity == ConsoleOutputSeverity::Error)
            color = { 1.00f, 0.35f, 0.35f, 1.0f };
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(entry.Text.c_str());
        ImGui::PopStyleColor();
    }

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
    ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}
