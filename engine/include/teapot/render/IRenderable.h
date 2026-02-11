#pragma once

class IGraphicsAPI;

//=============================================================================
// IRenderable
//
// Interface for any object that can be rendered. Does not assume 2D or 3D.
//
// Concrete renderables register themselves into a RefBatch<IRenderable>
// (via RefBatchHandle) so that RenderSystem can iterate all active
// renderables efficiently each frame.
//
// Extensibility:
//   - GetRenderOrder() controls draw order (lower values first).
//     RenderSystem sorts the batch array by this value when dirty.
//   - IsVisible() enables cheap per-frame skipping without mutating
//     the batch array. Invisible renderables stay registered.
//   - Render() receives the backend-agnostic IGraphicsAPI for the
//     current RenderContext.
//=============================================================================
class IRenderable
{
public:
	virtual ~IRenderable() = default;

	virtual void Render(IGraphicsAPI& graphicsAPI) = 0;
	virtual int GetRenderOrder() const { return 0; }
	virtual bool IsVisible() const { return true; }
};
