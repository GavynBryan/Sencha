#include <debug/ConsolePanel.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugLogEntry.h>
#include <imgui.h>
#include <cstring>
#include <string_view>

namespace
{
	// Colour per log level — RGBA floats matching ImGui's convention.
	constexpr ImVec4 LevelColour(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Debug:    return { 0.60f, 0.60f, 0.60f, 1.0f }; // grey
			case LogLevel::Info:     return { 1.00f, 1.00f, 1.00f, 1.0f }; // white
			case LogLevel::Warning:  return { 1.00f, 0.85f, 0.20f, 1.0f }; // yellow
			case LogLevel::Error:    return { 1.00f, 0.35f, 0.35f, 1.0f }; // red
			case LogLevel::Critical: return { 1.00f, 0.20f, 0.80f, 1.0f }; // magenta
			default:                 return { 1.00f, 1.00f, 1.00f, 1.0f };
		}
	}

	constexpr const char* LevelLabel(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Debug:    return "DBG";
			case LogLevel::Info:     return "INF";
			case LogLevel::Warning:  return "WRN";
			case LogLevel::Error:    return "ERR";
			case LogLevel::Critical: return "CRT";
			default:                 return "???";
		}
	}
}

ConsolePanel::ConsolePanel(DebugLogSink& sink)
	: Sink(sink)
{
}

void ConsolePanel::Draw()
{
	ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Console"))
	{
		ImGui::End();
		return;
	}

	// -- Toolbar --------------------------------------------------------------

	if (ImGui::Button("Clear"))
	{
		Sink.Clear();
	}

	ImGui::SameLine();

	// Level filter checkboxes.
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
	ImGui::InputText("Category", CategoryFilterBuf, CategoryFilterBufSize);

	ImGui::Separator();

	// -- Entry list -----------------------------------------------------------

	ImGui::BeginChild("ScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

	const std::size_t count = Sink.Count();
	const std::string_view categoryFilter(CategoryFilterBuf);

	for (std::size_t i = 0; i < count; ++i)
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
		ImGui::TextUnformatted(LevelLabel(e.Level));
		ImGui::PopStyleColor();

		ImGui::SameLine();
		ImGui::TextDisabled("[%s]", e.Category.c_str());
		ImGui::SameLine();
		ImGui::TextUnformatted(e.Message.c_str());
	}

	if (ScrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		ImGui::SetScrollHereY(1.0f);
	ScrollToBottom = false;

	ImGui::EndChild();
	ImGui::End();
}
