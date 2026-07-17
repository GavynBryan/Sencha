#include <debug/RenderStatsPanel.h>

#include <imgui.h>

#include <cinttypes>

RenderStatsPanel::RenderStatsPanel(const RenderProfileMode& activeMode,
                                   const RenderStatsHistory& history)
	: ActiveMode(activeMode)
	, History(history)
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

	ImGui::Separator();
	ImGui::Text("Lights");
	ImGui::Text("  packed %u  dropped at cap %u  shadow-casting %u",
	            stats->LightsVisible, stats->LightsDroppedAtCap,
	            stats->ShadowCastingLights);

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

	ImGui::Separator();
	ImGui::Text("Frame services");
	ImGui::Text("  scratch high water %.1f KiB",
	            static_cast<float>(stats->ScratchHighWaterBytes) / 1024.0f);

	ImGui::End();
}
