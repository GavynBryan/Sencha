#pragma once

#include <system/ISystem.h>

class ServiceProvider;
class RenderContextService;
class IRenderable;
template<typename T> class BatchArray;

//=============================================================================
// RenderSystem
//
// System that renders all active RenderContexts each frame. For each
// context, it runs the frame lifecycle (BeginFrame -> Clear -> Render ->
// EndFrame -> Present) and draws all IRenderables registered in the
// BatchArray<IRenderable>.
//
// Dependencies are resolved from a ServiceProvider at construction time.
// The system caches only the specific service references it needs.
//=============================================================================
class RenderSystem : public ISystem
{
public:
	explicit RenderSystem(const ServiceProvider& provider);

private:
	void Update() override;

	RenderContextService& ContextService;
	BatchArray<IRenderable>& Renderables;
};
