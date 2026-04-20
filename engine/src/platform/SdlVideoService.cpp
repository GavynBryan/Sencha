#include <platform/SdlVideoService.h>

#include <SDL3/SDL.h>

SdlVideoService::SdlVideoService(LoggingProvider& logging)
    : Log(logging.GetLogger<SdlVideoService>())
{
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0)
    {
        Initialized = true;
        Log.Info("SDL video subsystem already initialized");
        return;
    }

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        Log.Error("SDL video init failed: {}", SDL_GetError());
        return;
    }

    Initialized = true;
    OwnsVideoSubsystem = true;
    Log.Info("SDL video subsystem initialized");
}

SdlVideoService::~SdlVideoService()
{
    if (OwnsVideoSubsystem)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        Log.Info("SDL video subsystem shut down");
    }
}
