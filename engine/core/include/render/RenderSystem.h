#pragma once

#include <system/ISystem.h>

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
// Dependencies are injected explicitly via the constructor:
//   - RenderContextService& : provides the set of render targets
//   - BatchArray<IRenderable>& : provides the set of renderable objects
//=============================================================================
class RenderSystem : public ISystem
{
public:
	RenderSystem(RenderContextService& contextService, BatchArray<IRenderable>& renderables);

private:
	void Update() override;

	RenderContextService& ContextService;
	BatchArray<IRenderable>& Renderables;
};
