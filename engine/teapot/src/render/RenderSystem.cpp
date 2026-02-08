#include <render/RenderSystem.h>
#include <render/RenderContextService.h>
#include <render/IRenderable.h>
#include <render/IGraphicsAPI.h>
#include <service/BatchArray.h>
#include <service/ServiceProvider.h>

RenderSystem::RenderSystem(const ServiceProvider& provider)
	: ContextService(provider.Get<RenderContextService>())
	, Renderables(provider.Get<BatchArray<IRenderable>>())
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
