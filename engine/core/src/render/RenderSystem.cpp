#include <render/RenderSystem.h>
#include <render/RenderContextService.h>
#include <render/IRenderable.h>
#include <render/IGraphicsAPI.h>
#include <service/BatchArray.h>

RenderSystem::RenderSystem(RenderContextService& contextService, BatchArray<IRenderable>& renderables)
	: ContextService(contextService)
	, Renderables(renderables)
{
}

void RenderSystem::Update()
{
	Renderables.SortIfDirty([](IRenderable* a, IRenderable* b)
	{
		return a->GetRenderOrder() < b->GetRenderOrder();
	});

	for (auto& context : ContextService.GetContexts())
	{
		if (!context.bIsActive || !context.GraphicsAPI)
		{
			continue;
		}

		auto& api = *context.GraphicsAPI;

		if (!api.IsValid())
		{
			continue;
		}

		api.BeginFrame();
		api.Clear();

		for (auto* renderable : Renderables.GetItems())
		{
			if (renderable->IsVisible())
			{
				renderable->Render(api);
			}
		}

		api.EndFrame();
		api.Present();
	}
}
