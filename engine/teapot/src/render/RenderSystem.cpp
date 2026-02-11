#include <render/RenderSystem.h>
#include <render/RenderContextService.h>
#include <render/IRenderable.h>
#include <render/IGraphicsAPI.h>
#include <batch/RefBatch.h>
#include <service/ServiceProvider.h>
#include <logging/Logger.h>

RenderSystem::RenderSystem(const ServiceProvider& provider)
	: ContextService(provider.Get<RenderContextService>())
	, Renderables(provider.Get<RefBatch<IRenderable>>())
	, Log(provider.GetLogger<RenderSystem>())
{
	if(!&Renderables)
	{
		Log.Error("RenderSystem initialized without a RefBatch<IRenderable> service!");
	}
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
