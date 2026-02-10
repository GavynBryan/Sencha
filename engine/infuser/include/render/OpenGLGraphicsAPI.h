#pragma once

#include <render/IGraphicsAPI.h>

struct SDL_Window;

//=============================================================================
// OpenGLGraphicsAPI
//
// IGraphicsAPI implementation using OpenGL 3.3 core profile with SDL2
// for buffer swapping. Manages the frame lifecycle: clearing the
// framebuffer, presenting the backbuffer via SDL_GL_SwapWindow.
//
// Does not own the SDL_Window — the caller (typically SDLWindow) retains
// ownership. This class only needs the handle for Present().
//=============================================================================
class OpenGLGraphicsAPI : public IGraphicsAPI
{
public:
	explicit OpenGLGraphicsAPI(SDL_Window* window);

	bool IsValid() const override;
	void BeginFrame() override;
	void Clear() override;
	void EndFrame() override;
	void Present() override;

	void SetClearColor(float r, float g, float b, float a = 1.0f);

private:
	SDL_Window* Window;
};
