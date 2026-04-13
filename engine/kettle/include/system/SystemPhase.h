#pragma once

enum class SystemPhase : int
{
	Input      = 100,  // Raw hardware event ingestion
	PreUpdate  = 200,  // Input mapping, AI decisions, pre-simulation
	Update     = 300,  // Main simulation (physics, gameplay)
	PostUpdate = 400,  // Post-simulation (transform propagation, etc.)
	PreRender  = 500,  // Prepare render data
	Render     = 600,  // Rendering
};
