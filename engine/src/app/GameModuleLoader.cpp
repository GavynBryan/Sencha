#include <app/GameModuleLoader.h>

#include <string>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace
{
    void SetError(std::string* error, std::string message)
    {
        if (error)
            *error = std::move(message);
    }

#if defined(_WIN32)
    void*  OpenLibrary(const char* path)         { return ::LoadLibraryA(path); }
    void*  ResolveSymbol(void* h, const char* s) { return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(h), s)); }
    void   CloseLibrary(void* h)                 { ::FreeLibrary(static_cast<HMODULE>(h)); }
    std::string LastError()                      { return "LoadLibrary/GetProcAddress failed"; }
#else
    void*  OpenLibrary(const char* path)         { return ::dlopen(path, RTLD_NOW | RTLD_LOCAL); }
    void*  ResolveSymbol(void* h, const char* s) { return ::dlsym(h, s); }
    void   CloseLibrary(void* h)                 { ::dlclose(h); }
    std::string LastError()                      { const char* e = ::dlerror(); return e ? e : "unknown error"; }
#endif

    using FactoryFn = IGameModule* (*)();
}

LoadedModule GameModuleLoader::Load(const std::filesystem::path& artifact,
                                    GameModuleContext& ctx,
                                    std::string* error)
{
    void* handle = OpenLibrary(artifact.string().c_str());
    if (!handle)
    {
        SetError(error, "Failed to open game module '" + artifact.string() + "': " + LastError());
        return {};
    }

    auto factory = reinterpret_cast<FactoryFn>(ResolveSymbol(handle, "SenchaCreateGameModule"));
    if (!factory)
    {
        SetError(error, "Game module '" + artifact.string() +
                            "' is missing the SenchaCreateGameModule entry point.");
        CloseLibrary(handle);
        return {};
    }

    IGameModule* module = factory();
    if (!module)
    {
        SetError(error, "SenchaCreateGameModule returned null.");
        CloseLibrary(handle);
        return {};
    }

    // ABI check FIRST — never call Register on an incompatible build.
    if (module->AbiVersion() != SENCHA_GAME_ABI_VERSION)
    {
        SetError(error, "Game module ABI mismatch: module reports " +
                            std::to_string(module->AbiVersion()) + ", host expects " +
                            std::to_string(SENCHA_GAME_ABI_VERSION) + ".");
        CloseLibrary(handle);
        return {};
    }

    module->Register(ctx);
    return LoadedModule{ handle, module };
}

void GameModuleLoader::Unload(LoadedModule& module, GameModuleContext& ctx)
{
    if (module.Module)
        module.Module->Unregister(ctx);
    if (module.Handle)
        CloseLibrary(module.Handle);
    module = {};
}
