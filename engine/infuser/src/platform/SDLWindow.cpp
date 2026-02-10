#include <platform/SDLWindow.h>
#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <stdexcept>

SDLWindow::SDLWindow(const char* title, int width, int height)
	: Width(width)
	, Height(height)
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		throw std::runtime_error(
			std::string("SDL_Init failed: ") + SDL_GetError());
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	Window = SDL_CreateWindow(
		title,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		width, height,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);

	if (!Window)
	{
		SDL_Quit();
		throw std::runtime_error(
			std::string("SDL_CreateWindow failed: ") + SDL_GetError());
	}

	Context = SDL_GL_CreateContext(Window);
	if (!Context)
	{
		SDL_DestroyWindow(Window);
		SDL_Quit();
		throw std::runtime_error(
			std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
	}

	glewExperimental = GL_TRUE;
	GLenum glewStatus = glewInit();
	if (glewStatus != GLEW_OK)
	{
		SDL_GL_DeleteContext(Context);
		SDL_DestroyWindow(Window);
		SDL_Quit();
		throw std::runtime_error(
			std::string("glewInit failed: ") +
			reinterpret_cast<const char*>(glewGetErrorString(glewStatus)));
	}

	SDL_GL_SetSwapInterval(1);
}

SDLWindow::~SDLWindow()
{
	if (Context)
	{
		SDL_GL_DeleteContext(Context);
	}
	if (Window)
	{
		SDL_DestroyWindow(Window);
	}
	SDL_Quit();
}

float SDLWindow::GetAspectRatio() const
{
	if (Height == 0) return 1.0f;
	return static_cast<float>(Width) / static_cast<float>(Height);
}

void SDLWindow::SetRelativeMouseMode(bool enabled)
{
	SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}
