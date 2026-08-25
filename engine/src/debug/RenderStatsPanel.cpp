#include <debug/RenderStatsPanel.h>

#include <core/console/ConsoleRegistry.h>
#include <render/RenderDebugView.h>

#include <imgui.h>

#include <cinttypes>

RenderStatsPanel::RenderStatsPanel(const RenderProfileMode& activeMode,
                                   const RenderStatsHistory& history,
                                   ConsoleRegistry& console)
	: ActiveMode(activeMode)
	, History(history)
	, Console(console)
{
}

void RenderStatsPanel::Draw()
{
	if (!ImGui::Begin("Render Stats"))
	{
		ImGui::End();
		return;
	}

	ImGui::Text("render.profile.mode: %s", ToString(ActiveMode));

	RenderDebugView debugView = RenderDebugView::None;
	if (const CVarMetadata* cvar = Console.FindCVar("render.debug.view");
	    cvar != nullptr && std::holds_alternative<std::string>(cvar->CurrentValue))
	{
		(void)ParseRenderDebugView(
			std::get<std::string>(cvar->CurrentValue), debugView);
	}
	if (ImGui::BeginCombo("Debug view", RenderDebugViewLabel(debugView)))
	{
		for (std::uint32_t index = 0; index < kRenderDebugViewCount; ++index)
		{
			const RenderDebugView candidate = static_cast<RenderDebugView>(index);
			const bool selected = candidate == debugView;
			if (ImGui::Selectable(RenderDebugViewLabel(candidate), selected))
			{
				(void)Console.SetCVar(
					"render.debug.view", std::string{ ToString(candidate) },
					{ "Render Stats panel" }, ConsolePhase::GameplayStarted);
			}
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	if (ActiveMode < RenderProfileMode::Counters)
	{
		ImGui::TextDisabled("Counters off. Set render.profile.mode counters.");
		ImGui::End();
		return;
	}

	const RenderStats* stats = History.Latest();
	if (stats == nullptr)
	{
		ImGui::TextDisabled("No completed frame yet.");
		ImGui::End();
		return;
	}

	ImGui::Text("frame %" PRIu64, stats->FrameIndex);
	ImGui::Separator();

	ImGui::Text("Forward pass");
	ImGui::Text("  objects %u  draws %u  tris %u",
	            stats->VisibleObjects, stats->DrawCalls, stats->SubmittedTriangles);
	ImGui::Text("  pipeline switches %u  material switches %u",
	            stats->PipelineSwitches, stats->MaterialSwitches);
	if (stats->InstancesDropped > 0)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
		                   "  instances dropped %u", stats->InstancesDropped);
	}

	ImGui::Separator();
	ImGui::Text("Lights");
	ImGui::Text("  packed %u  dropped at cap %u  shadow-casting %u",
	            stats->LightsVisible, stats->LightsDroppedAtCap,
	            stats->ShadowCastingLights);
	ImGui::Text("  probe volumes resident %u", stats->ProbeVolumesResident);

	ImGui::Separator();
	ImGui::Text("Shadows");
	ImGui::Text("  slots %u  denied %u  views rendered %u  caster draws %u",
	            stats->ShadowSlotsHeld, stats->ShadowRequestsDenied,
	            stats->ShadowViewsRendered, stats->ShadowCasterDraws);
	const std::uint32_t cacheable = stats->ShadowCacheHits + stats->ShadowViewsRendered;
	ImGui::Text("  cache hits %u (%.0f%%)  diff events %u",
	            stats->ShadowCacheHits,
	            cacheable > 0
	                ? 100.0f * static_cast<float>(stats->ShadowCacheHits)
	                    / static_cast<float>(cacheable)
	                : 0.0f,
	            stats->CasterDiffEvents);
	ImGui::Text("  tiles 1024:%u 512:%u 256:%u  atlas %.1f MiB",
	            stats->AtlasTiles1024, stats->AtlasTiles512, stats->AtlasTiles256,
	            static_cast<float>(stats->ShadowTileBytes) / (1024.0f * 1024.0f));
	ImGui::Text("  point cubes %u  faces rendered %u",
	            stats->PointShadowCubesHeld, stats->PointShadowFacesRendered);
	ImGui::Text("  casters tested %u  visible %u  runs %u",
	            stats->ShadowCastersTested, stats->ShadowCastersVisible,
	            stats->ShadowInstanceRuns);

	ImGui::Separator();
	ImGui::Text("Frame services");
	ImGui::Text("  scratch %.1f / %.1f KiB (high water %.1f KiB)",
	            static_cast<float>(stats->ScratchUsedBytes) / 1024.0f,
	            static_cast<float>(stats->ScratchBytesPerFrame) / 1024.0f,
	            static_cast<float>(stats->ScratchHighWaterBytes) / 1024.0f);
	if (stats->ScratchAllocFailures > 0 || stats->PassesSkipped > 0)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
		                   "  scratch failures %u  passes skipped %u",
		                   stats->ScratchAllocFailures, stats->PassesSkipped);
	}
	// Per consumer, so an exhausted slice names who filled it rather than only
	// who happened to ask last.
	for (std::size_t i = 0; i < ScratchTagCounters::kTagCount; ++i)
	{
		const std::uint64_t high = stats->ScratchTagHighWaterBytes[i];
		if (high == 0 && stats->ScratchTagFailures[i] == 0)
			continue;
		const char* name = ToString(static_cast<ScratchTag>(i)).data();
		if (stats->ScratchTagFailures[i] > 0)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
			                   "    %s %.1f KiB peak, %u failed", name,
			                   static_cast<float>(high) / 1024.0f,
			                   stats->ScratchTagFailures[i]);
		}
		else
		{
			ImGui::Text("    %s %.1f KiB peak", name,
			            static_cast<float>(high) / 1024.0f);
		}
	}

	ImGui::End();
}
