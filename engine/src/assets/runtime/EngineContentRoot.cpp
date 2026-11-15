#include <assets/runtime/EngineContentRoot.h>

#include <SDL3/SDL.h>

#include <system_error>

std::filesystem::path EngineContentRoot()
{
    const auto usable = [](const std::filesystem::path& candidate) {
        std::error_code ec;
        return !candidate.empty() && std::filesystem::is_directory(candidate, ec) && !ec;
    };

    if (const char* override = SDL_getenv_unsafe("SENCHA_ENGINE_CONTENT");
        override != nullptr && override[0] != '\0')
    {
        const std::filesystem::path path(override);
        if (usable(path))
            return path;
    }

    if (const char* base = SDL_GetBasePath(); base != nullptr)
    {
        const std::filesystem::path installed =
            std::filesystem::path(base) / ".." / "share" / "sencha" / "content";
        if (usable(installed))
            return installed.lexically_normal();
    }

#ifdef SENCHA_ENGINE_CONTENT_DIR
    if (const std::filesystem::path source(SENCHA_ENGINE_CONTENT_DIR); usable(source))
        return source;
#endif
    return {};
}
