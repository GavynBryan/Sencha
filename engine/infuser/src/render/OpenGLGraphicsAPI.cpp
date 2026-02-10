#include <render/OpenGLGraphicsAPI.h>
#include <GL/glew.h>
#include <SDL2/SDL.h>

OpenGLGraphicsAPI::OpenGLGraphicsAPI(SDL_Window* window)
	: Window(window)
{
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
}

bool OpenGLGraphicsAPI::IsValid() const
{
	return Window != nullptr;
}

void OpenGLGraphicsAPI::BeginFrame()
{
}

void OpenGLGraphicsAPI::Clear()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLGraphicsAPI::EndFrame()
{
}

void OpenGLGraphicsAPI::Present()
{
	SDL_GL_SwapWindow(Window);
}

void OpenGLGraphicsAPI::SetClearColor(float r, float g, float b, float a)
{
	glClearColor(r, g, b, a);
}
