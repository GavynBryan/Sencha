#include <debug/ConsolePanel.h>

#include <debug/ConsoleView.h>

#include <imgui.h>

namespace
{
	// Plain colors for the runtime overlay (no editor palette / mono font).
	ConsoleViewStyle RuntimeStyle()
	{
		ConsoleViewStyle style;
		style.LevelColors = {
			ImVec4{ 0.60f, 0.60f, 0.60f, 1.0f }, // Debug    grey
			ImVec4{ 1.00f, 1.00f, 1.00f, 1.0f }, // Info     white
			ImVec4{ 1.00f, 0.85f, 0.20f, 1.0f }, // Warning  yellow
			ImVec4{ 1.00f, 0.35f, 0.35f, 1.0f }, // Error    red
			ImVec4{ 1.00f, 0.20f, 0.80f, 1.0f }, // Critical magenta
		};
		style.TextPrimary = ImVec4{ 0.80f, 0.80f, 0.80f, 1.0f };
		style.Warning = ImVec4{ 1.00f, 0.85f, 0.20f, 1.0f };
		style.Error = ImVec4{ 1.00f, 0.35f, 0.35f, 1.0f };
		return style;
	}
}

ConsolePanel::ConsolePanel(DebugLogSink& sink, ConsoleService& console)
	: Sink(sink)
	, Console(console)
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

	DrawConsoleView(Sink, Console, State, RuntimeStyle());

	ImGui::End();
}
