#pragma once

#include <debug/IDebugPanel.h>
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>

//=============================================================================
// RenderStatsPanel
//
// Debug-overlay window over the renderer counter history: the active
// render.profile.mode, the last completed frame's RenderStats grouped by
// producer (forward pass, lights, shadow residency, frame services), and the
// shadow cache hit rate. Reads the engine-owned mode and ring by reference;
// draws a hint instead of numbers while the mode is Off.
//=============================================================================
class RenderStatsPanel : public IDebugPanel
{
public:
	RenderStatsPanel(const RenderProfileMode& activeMode,
	                 const RenderStatsHistory& history);

	void Draw() override;

private:
	const RenderProfileMode& ActiveMode;
	const RenderStatsHistory& History;
};
