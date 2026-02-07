#pragma once

#include <cstdint>

class IGraphicsAPI;

//=============================================================================
// RenderContext
//
// Represents a single rendering target (e.g. a window or offscreen surface).
// Does not assume any specific windowing backend (SDL, GLFW, etc.) or
// graphics backend (SFML, OpenGL, DirectX, etc.).
//
// Each context holds a pointer to the IGraphicsAPI responsible for
// rendering into that target. Multiple contexts enable multi-window
// rendering.
//=============================================================================
struct RenderContext
{
	uint32_t Id = 0;
	IGraphicsAPI* GraphicsAPI = nullptr;
	bool bIsActive = true;
};
