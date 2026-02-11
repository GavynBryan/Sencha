#pragma once

#include <kettle/system/ISystem.h>

class ServiceProvider;
class RenderContextService;
class IRenderable;
class Logger;
template<typename T> class RefBatch;

//=============================================================================
// RenderSystem
//
// Virtual-dispatch render system for custom renderables. Iterates a
// RefBatch<IRenderable> each frame and calls Render() on each visible
// entry for every active RenderContext.
//
// For cache-friendly, data-oriented rendering prefer RenderSystem2D or
// RenderSystem3D, which iterate DataBatch<RenderData2D/3D> and call
// IGraphicsAPI::Submit2D/Submit3D with plain data. This system remains
// available as an escape hatch for objects that need bespoke rendering
// logic (debug overlays, procedural effects, etc.).
//
// Dependencies (resolved from ServiceProvider at construction):
//   - RenderContextService    — all render targets
//   - RefBatch<IRenderable>   — all active custom renderables
//   - Logger                  — logging
//=============================================================================
class RenderSystem : public ISystem
{
public:
	explicit RenderSystem(const ServiceProvider& provider);

private:
	void Update() override;

	RenderContextService& ContextService;
	RefBatch<IRenderable>& Renderables;
	Logger& Log;
};
