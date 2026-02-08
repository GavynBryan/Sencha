#pragma once

//=============================================================================
// IGraphicsAPI
//
// Backend-agnostic interface for graphics operations. Does not assume any
// specific rendering backend (SFML, OpenGL, SDL, DirectX) or windowing
// system (SDL, GLFW, etc.).
//
// Implementations live at the COMMON layer of the engine, not in teapot or
// kettle. Each RenderContext holds a pointer to an IGraphicsAPI that
// handles the actual rendering for that context's window/surface.
//
// IsValid() reports whether the underlying target is still usable (e.g.
// window open, device not lost). RenderSystem skips invalid APIs.
//
// Frame lifecycle:
//   IsValid() -> BeginFrame() -> Clear() -> [draw calls] -> EndFrame() -> Present()
//=============================================================================
class IGraphicsAPI
{
public:
	virtual ~IGraphicsAPI() = default;

	virtual bool IsValid() const = 0;
	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;
	virtual void Clear() = 0;
	virtual void Present() = 0;
};
