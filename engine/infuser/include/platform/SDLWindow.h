#pragma once

#include <service/IService.h>
#include <cstdint>

struct SDL_Window;
using SDL_GLContext = void*;

//=============================================================================
// SDLWindow
//
// Manages an SDL2 window with an OpenGL context. Handles creation,
// destruction, and provides access to the underlying handles that
// graphics backends need.
//
// Owns the SDL_Window and SDL_GLContext lifetime. Initializes SDL
// video subsystem on construction and cleans up on destruction.
//=============================================================================
class SDLWindow : public IService
{
public:
	SDLWindow(const char* title, int width, int height);
	~SDLWindow() override;

	SDLWindow(const SDLWindow&) = delete;
	SDLWindow& operator=(const SDLWindow&) = delete;

	SDL_Window* GetHandle() const { return Window; }
	SDL_GLContext GetGLContext() const { return Context; }

	int GetWidth() const { return Width; }
	int GetHeight() const { return Height; }
	float GetAspectRatio() const;

	bool IsOpen() const { return bIsOpen; }
	void Close() { bIsOpen = false; }

	void SetRelativeMouseMode(bool enabled);

private:
	SDL_Window* Window = nullptr;
	SDL_GLContext Context = nullptr;
	int Width = 0;
	int Height = 0;
	bool bIsOpen = true;
};
