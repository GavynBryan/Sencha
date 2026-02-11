#pragma once

#include <kettle/system/ISystem.h>

class ServiceProvider;
class RenderContextService;
class Logger;
struct RenderData2D;
template<typename T> class DataBatch;

//=============================================================================
// RenderSystem2D
//
// Data-oriented render system for 2D objects. Iterates a
// DataBatch<RenderData2D> each frame, submitting visible entries to every
// active RenderContext's IGraphicsAPI via Submit2D().
//
// DataBatch stores RenderData2D values contiguously in memory, giving
// cache-friendly iteration without virtual dispatch or pointer chasing.
//
// Dependencies (resolved from ServiceProvider at construction):
//   - RenderContextService  — all render targets
//   - DataBatch<RenderData2D> — all active 2D renderables
//   - Logger                — logging
//=============================================================================
class RenderSystem2D : public ISystem
{
public:
	explicit RenderSystem2D(const ServiceProvider& provider);

private:
	void Update() override;

	RenderContextService& ContextService;
	DataBatch<RenderData2D>& Renderables;
	Logger& Log;
};
