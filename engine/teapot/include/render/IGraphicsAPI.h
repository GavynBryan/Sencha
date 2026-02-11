#pragma once

#include <math/Vec.h>

//=============================================================================
// IGraphicsAPI
//
// Backend-agnostic interface for graphics operations. Does not assume any
// specific rendering backend (GLEW/OpenGL, SDL, DirectX) or windowing
// system (SDL, GLFW, etc.).
//
// Concrete implementations live in the Infuser layer. Each RenderContext
// holds a pointer to an IGraphicsAPI that handles the actual rendering
// for that context's window/surface.
//
// IsValid() reports whether the underlying target is still usable (e.g.
// window open, device not lost). Render systems skip invalid APIs.
//
// Frame lifecycle:
//   IsValid() -> BeginFrame() -> Clear() -> [submit calls] -> EndFrame() -> Present()
//
// Submit methods:
//   Submit2D() and Submit3D() are the data-oriented draw entry points.
//   Render systems iterate DataBatch contents and call these with plain
//   data — no virtual dispatch on the renderable side. Backends that
//   don't support a dimension leave the default no-op.
//=============================================================================
class IGraphicsAPI
{
public:
	virtual ~IGraphicsAPI() = default;

	// -- Frame lifecycle ----------------------------------------------------

	virtual bool IsValid() const = 0;
	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;
	virtual void Clear() = 0;
	virtual void Present() = 0;

	// -- Configuration ------------------------------------------------------

	virtual void SetClearColor(float r, float g, float b, float a) { (void)r; (void)g; (void)b; (void)a; }

	// -- 2D submission ------------------------------------------------------

	virtual void Submit2D(const Vec2& position, const Vec2& scale, float rotation) { (void)position; (void)scale; (void)rotation; }

	// -- 3D submission ------------------------------------------------------

	virtual void Submit3D(const Vec3& position, const Vec3& scale, const Vec3& rotation) { (void)position; (void)scale; (void)rotation; }
};
