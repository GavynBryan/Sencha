#pragma once

#include <kettle/system/ISystem.h>

class ServiceProvider;
class RenderContextService;
class Logger;
struct RenderData3D;
template<typename T> class DataBatch;

//=============================================================================
// RenderSystem3D
//
// Data-oriented render system for 3D objects. Iterates a
// DataBatch<RenderData3D> each frame, submitting visible entries to every
// active RenderContext's IGraphicsAPI via Submit3D().
//
// DataBatch stores RenderData3D values contiguously in memory, giving
// cache-friendly iteration without virtual dispatch or pointer chasing.
//
// Dependencies (resolved from ServiceProvider at construction):
//   - RenderContextService  — all render targets
//   - DataBatch<RenderData3D> — all active 3D renderables
//   - Logger                — logging
//=============================================================================
class RenderSystem3D : public ISystem
{
public:
	explicit RenderSystem3D(const ServiceProvider& provider);

private:
	void Update() override;

	RenderContextService& ContextService;
	DataBatch<RenderData3D>& Renderables;
	Logger& Log;
};
