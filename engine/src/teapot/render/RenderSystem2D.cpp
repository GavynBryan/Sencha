#include <teapot/render/RenderSystem2D.h>
#include <teapot/render/RenderContextService.h>
#include <teapot/render/RenderData.h>
#include <teapot/render/IGraphicsAPI.h>
#include <kettle/batch/DataBatch.h>
#include <kettle/service/ServiceProvider.h>
#include <kettle/logging/Logger.h>

RenderSystem2D::RenderSystem2D(const ServiceProvider& provider)
	: ContextService(provider.Get<RenderContextService>())
	, Renderables(provider.Get<DataBatch<RenderData2D>>())
	, Log(provider.GetLogger<RenderSystem2D>())
{
}

void RenderSystem2D::Update()
{
	Renderables.SortIfDirty([](const RenderData2D& a, const RenderData2D& b)
	{
		return a.RenderOrder < b.RenderOrder;
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

		for (const auto& data : Renderables)
		{
			if (data.bIsVisible)
			{
				api.Submit2D(data.Transform);
			}
		}

		api.EndFrame();
		api.Present();
	}
}
