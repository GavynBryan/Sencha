#include <render/RenderContextService.h>
#include <render/RenderSystem.h>
#include <batch/BatchArray.h>
#include <batch/BatchArrayHandle.h>
#include <service/ServiceHost.h>
#include <service/ServiceProvider.h>
#include <system/SystemHost.h>
#include <logging/ConsoleLogSink.h>
#include <platform/SDLWindow.h>
#include <render/OpenGLGraphicsAPI.h>
#include <SDL2/SDL.h>

#include "Maze.h"
#include "MazeRenderer.h"
#include "PlayerSystem.h"

#include <cmath>

//=============================================================================
// Maze Example
//
// First-person 3D maze with AABB collision. Demonstrates Sencha's
// service-oriented architecture with concrete SDL/OpenGL backends
// from the infuser layer.
//
// Services:
//   SDLWindow          — owns the OS window and GL context
//   RenderContextService — manages render targets
//   BatchArray<IRenderable> — renderable registry
//   Maze               — maze grid data
//   CameraState        — first-person camera
//   InputState         — per-frame input snapshot
//
// Systems:
//   PlayerSystem (order 0) — reads input, moves camera, collision
//   RenderSystem (order 1) — iterates renderables, draws frame
//=============================================================================

static constexpr int WindowWidth = 1024;
static constexpr int WindowHeight = 768;
static constexpr int MazeWidth = 21;
static constexpr int MazeHeight = 21;

int main()
{
	// -- Services -----------------------------------------------------------

	ServiceHost services;

	auto& logging = services.GetLoggingProvider();
	logging.SetMinLevel(LogLevel::Info);
	logging.AddSink<ConsoleLogSink>();

	auto& logger = logging.GetLogger<Maze>();

	auto& window = services.AddService<SDLWindow>("Sencha — Maze", WindowWidth, WindowHeight);
	window.SetRelativeMouseMode(true);

	OpenGLGraphicsAPI glAPI(window.GetHandle());
	glAPI.SetClearColor(0.05f, 0.05f, 0.08f);

	services.AddService<RenderContextService>(logging);
	services.AddService<BatchArray<IRenderable>>();
	services.AddService<Maze>(MazeWidth, MazeHeight);
	services.AddService<InputState>();

	auto& cameraState = services.AddService<CameraState>();
	cameraState.Position = services.Get<Maze>().GetSpawnPosition();
	cameraState.Projection = Mat4f::Perspective(
		1.2f, window.GetAspectRatio(), 0.01f, 100.0f);

	auto& contexts = services.Get<RenderContextService>();
	contexts.AddContext(&glAPI);

	auto& renderables = services.Get<BatchArray<IRenderable>>();

	// -- Renderables --------------------------------------------------------

	MazeRenderer mazeRenderer(services.Get<Maze>(), cameraState);
	BatchArrayHandle hMaze(&renderables, &mazeRenderer);

	logger.Info("Maze generated ({}x{}), {} vertices",
		MazeWidth, MazeHeight, 0);

	// -- Systems ------------------------------------------------------------

	ServiceProvider provider(services);

	SystemHost systems;
	systems.AddSystem<PlayerSystem>(0, provider);
	systems.AddSystem<RenderSystem>(1, provider);
	systems.Init();

	logger.Info("Entering main loop. WASD to move, mouse to look, ESC to quit.");

	// -- Main loop ----------------------------------------------------------

	auto& input = services.Get<InputState>();
	Uint64 lastTicks = SDL_GetPerformanceCounter();
	Uint64 frequency = SDL_GetPerformanceFrequency();

	while (window.IsOpen() && !input.bQuit)
	{
		// Delta time
		Uint64 now = SDL_GetPerformanceCounter();
		float dt = static_cast<float>(now - lastTicks) /
		           static_cast<float>(frequency);
		lastTicks = now;
		input.DeltaTime = dt;

		// Poll events
		input.MouseDeltaX = 0.0f;
		input.MouseDeltaY = 0.0f;

		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:
				input.bQuit = true;
				break;
			case SDL_KEYDOWN:
				if (event.key.keysym.sym == SDLK_ESCAPE)
					input.bQuit = true;
				break;
			case SDL_MOUSEMOTION:
				input.MouseDeltaX += static_cast<float>(event.motion.xrel);
				input.MouseDeltaY -= static_cast<float>(event.motion.yrel);
				break;
			}
		}

		// Keyboard state (held keys)
		const Uint8* keys = SDL_GetKeyboardState(nullptr);
		input.bForward  = keys[SDL_SCANCODE_W];
		input.bBackward = keys[SDL_SCANCODE_S];
		input.bLeft     = keys[SDL_SCANCODE_A];
		input.bRight    = keys[SDL_SCANCODE_D];

		systems.Update();
	}

	// -- Shutdown -----------------------------------------------------------

	systems.Shutdown();
	logger.Info("Maze example finished.");

	return 0;
}
