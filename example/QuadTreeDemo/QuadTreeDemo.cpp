#include "QuadTreeDemoGame.h"
#include "QuadTreeDemoInput.h"
#include "QuadTreeDemoRender.h"

#include <core/logging/ConsoleLogSink.h>
#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <core/system/SystemPhase.h>
#include <input/InputBindingService.h>
#include <input/SdlInputSystem.h>
#include <render/Renderer.h>
#include <window/SdlWindowService.h>
#include <world/World.h>
#include <world/WorldSetup.h>

#include <SDL3/SDL.h>

#include <utility>

namespace
{
	class QuadTreeDemo
	{
	};

	bool IsRenderableExtent(WindowExtent extent)
	{
		return extent.Width > 0 && extent.Height > 0;
	}
}

int main()
{
	ServiceHost services;
	auto& logging = services.GetLoggingProvider();
	logging.AddSink<ConsoleLogSink>();

	auto& logger = logging.GetLogger<QuadTreeDemo>();

	auto inputTable = LoadQuadTreeDemoInputBindings(logger);
	if (!inputTable) return 1;

	QuadTreeDemoRenderBootstrap render(logging);
	if (!render.IsValid()) return 1;

	InputBindingService bindings;
	bindings.SetBindings(std::move(*inputTable));

	SystemHost systems;
	WorldSetup::Setup2D(services, systems);

	World2d& world = services.Get<World2d>();
	auto& state = services.AddService<QuadTreeDemoState>(world);

	auto& input = systems.AddSystem<SdlInputSystem>(SystemPhase::Input, logging, bindings);
	systems.AddSystem<QuadTreePlayerMovementSystem>(
		SystemPhase::Update,
		state,
		world,
		input.GetEvents());
	systems.AddSystem<QuadTreeRenderSystem>(
		SystemPhase::PreRender,
		state,
		world,
		render.Sprites(),
		render.WhiteTexture());

	systems.Init();

	logger.Info("QuadTree demo running. Arrow keys move the block. Escape quits.");

	while (state.Running && render.Windows().IsAlive(render.WindowId()))
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			render.Windows().HandleEvent(event);
			InjectQuadTreeDemoInputEvent(input, event);

			if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
			{
				state.Running = false;
			}
		}

		WindowExtent resizeExtent{};
		const bool resizePending = render.Windows().ConsumeResize(render.WindowId(), &resizeExtent);
		const auto* windowState = render.Windows().GetState(render.WindowId());
		const bool minimized = windowState && windowState->Minimized;
		WindowExtent currentExtent = resizePending
			? resizeExtent
			: render.Windows().GetExtent(render.WindowId());

		if (minimized || !IsRenderableExtent(currentExtent))
		{
			SDL_Delay(16);
			continue;
		}

		if (resizePending)
		{
			if (!render.RecreateSwapchain(currentExtent))
			{
				logger.Error("Failed to recreate swapchain");
				return 1;
			}
			continue;
		}

		systems.Update(FrameTime{});

		const auto status = render.DrawFrame();
		if (status == Renderer::DrawStatus::SwapchainOutOfDate)
		{
			currentExtent = render.Windows().GetExtent(render.WindowId());
			if (!render.RecreateSwapchain(currentExtent))
			{
				logger.Error("Failed to recreate swapchain");
				return 1;
			}
			continue;
		}
		if (status == Renderer::DrawStatus::Error)
		{
			return 1;
		}
	}

	systems.Shutdown();

	return 0;
}
