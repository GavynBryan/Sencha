#include <render/RenderContextService.h>
#include <algorithm>

uint32_t RenderContextService::AddContext(IGraphicsAPI* graphicsAPI)
{
	RenderContext context;
	context.Id = NextId++;
	context.GraphicsAPI = graphicsAPI;
	context.bIsActive = true;

	Contexts.push_back(context);
	return context.Id;
}

void RenderContextService::RemoveContext(uint32_t id)
{
	Contexts.erase(
		std::remove_if(Contexts.begin(), Contexts.end(),
			[id](const RenderContext& ctx) { return ctx.Id == id; }),
		Contexts.end());
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
