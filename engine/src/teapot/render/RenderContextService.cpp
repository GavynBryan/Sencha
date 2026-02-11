#include <teapot/render/RenderContextService.h>
#include <kettle/logging/LoggingProvider.h>
#include <kettle/logging/Logger.h>
#include <algorithm>

RenderContextService::RenderContextService(LoggingProvider& provider)
	: Log(provider.GetLogger<RenderContextService>())
{
}

uint32_t RenderContextService::AddContext(IGraphicsAPI* graphicsAPI)
{
	RenderContext context;
	context.Id = NextId++;
	context.GraphicsAPI = graphicsAPI;
	context.bIsActive = true;

	Contexts.push_back(context);
	Log.Info("Added RenderContext with ID {}", context.Id);
	return context.Id;
}

void RenderContextService::RemoveContext(uint32_t id)
{
	auto it = std::remove_if(Contexts.begin(), Contexts.end(),
		[id](const RenderContext& ctx) { return ctx.Id == id; });
	if (it != Contexts.end())
	{
		Contexts.erase(it, Contexts.end());
		Log.Info("Removed RenderContext with ID {}", id);
	}
	else
	{
		Log.Info("RenderContext with ID {} not found", id);
	}
}

RenderContext* RenderContextService::GetContext(uint32_t id)
{
	for (auto& ctx : Contexts)
	{
		if (ctx.Id == id)
		{
			return &ctx;
		}
	}
	return nullptr;
}

const RenderContext* RenderContextService::GetContext(uint32_t id) const
{
	for (const auto& ctx : Contexts)
	{
		if (ctx.Id == id)
		{
			return &ctx;
		}
	}
	return nullptr;
}

std::span<RenderContext> RenderContextService::GetContexts()
{
	return std::span<RenderContext>(Contexts);
}

std::span<const RenderContext> RenderContextService::GetContexts() const
{
	return std::span<const RenderContext>(Contexts);
}

size_t RenderContextService::Count() const
{
	return Contexts.size();
}

bool RenderContextService::IsEmpty() const
{
	return Contexts.empty();
}
