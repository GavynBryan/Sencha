#pragma once

#include <render/RenderContext.h>
#include <service/IService.h>
#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// RenderContextService
//
// Service that manages multiple RenderContexts, enabling multi-window
// rendering. Each context represents a distinct render target with its
// own IGraphicsAPI instance.
//
// Does not assume any specific windowing or graphics backend. The concrete
// IGraphicsAPI pointers are set by COMMON-layer code that knows the
// actual backend in use.
//=============================================================================
class RenderContextService : public IService
{
public:
	uint32_t AddContext(IGraphicsAPI* graphicsAPI);
	void RemoveContext(uint32_t id);

	RenderContext* GetContext(uint32_t id);
	const RenderContext* GetContext(uint32_t id) const;

	std::span<RenderContext> GetContexts();
	std::span<const RenderContext> GetContexts() const;

	size_t Count() const;
	bool IsEmpty() const;

private:
	std::vector<RenderContext> Contexts;
	uint32_t NextId = 0;
};
