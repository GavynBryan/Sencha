#include <render/RenderSystem3D.h>
#include <render/RenderContextService.h>
#include <render/RenderData.h>
#include <render/IGraphicsAPI.h>
#include <batch/DataBatch.h>
#include <service/ServiceProvider.h>
#include <logging/Logger.h>

RenderSystem3D::RenderSystem3D(const ServiceProvider& provider)
	: ContextService(provider.Get<RenderContextService>())
	, Renderables(provider.Get<DataBatch<RenderData3D>>())
	, Log(provider.GetLogger<RenderSystem3D>())
{
}

void RenderSystem3D::Update()
{
	Renderables.SortIfDirty([](const RenderData3D& a, const RenderData3D& b)
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
				api.Submit3D(data.Position, data.Scale, data.Rotation);
			}
		}

		api.EndFrame();
		api.Present();
	}
}
