#include <app/GameModuleLoader.h>

#include <app/GameModuleAbi.h>

#include <optional>
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
    using AbiFn = const GameModuleAbi* (*)();
}

std::optional<std::string> DescribeGameModuleAbiMismatch(const GameModuleAbi& m, const GameModuleAbi& host)
{
    const auto field = [](const char* what, std::uint64_t module, std::uint64_t expected) {
        return std::optional<std::string>(std::string(what) + ": module " +
            std::to_string(module) + ", host expects " + std::to_string(expected));
    };

    // StructSize first: a layout mismatch is caught before any other field is
    // trusted (StructSize is the descriptor's first member, offset 0).
    if (m.StructSize != host.StructSize)
        return field("ABI descriptor size", m.StructSize, host.StructSize);
    if (m.AbiVersion != host.AbiVersion)
        return field("ABI version", m.AbiVersion, host.AbiVersion);
    if (m.HeaderFingerprint != host.HeaderFingerprint)
        return std::optional<std::string>(
            "ABI header fingerprint differs (module built against different engine "
            "headers — rebuild the module against this engine)");
    if (m.CompilerId != host.CompilerId)
        return field("compiler", m.CompilerId, host.CompilerId);
    if (m.CompilerMajor != host.CompilerMajor)
        return field("compiler major version", m.CompilerMajor, host.CompilerMajor);
    if (m.StdLibId != host.StdLibId)
        return field("C++ standard library", m.StdLibId, host.StdLibId);
    if (m.StdLibVersion != host.StdLibVersion)
        return field("C++ standard library version", m.StdLibVersion, host.StdLibVersion);
    if (m.PointerBits != host.PointerBits)
        return field("pointer width", m.PointerBits, host.PointerBits);
    if (m.BuildConfig != host.BuildConfig)
        return field("build configuration (debug/sanitizer flags)", m.BuildConfig, host.BuildConfig);
    return std::nullopt;
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

    // Validate the ABI through the C-linkage descriptor BEFORE constructing or
    // calling into the module's C++ vtable — so a skewed build is refused, not
    // crashed on. (09-module-abi-hardening.md.)
    auto abiFn = reinterpret_cast<AbiFn>(ResolveSymbol(handle, "SenchaGameModuleAbi"));
    if (!abiFn)
    {
        SetError(error, "Game module '" + artifact.string() +
                            "' is missing its ABI descriptor (SenchaGameModuleAbi) — rebuild it "
                            "against this engine (SENCHA_EXPORT_GAME_MODULE_ABI).");
        CloseLibrary(handle);
        return {};
    }

    const GameModuleAbi* moduleAbi = abiFn();
    const GameModuleAbi hostAbi = SenchaThisBuildAbi();
    if (moduleAbi == nullptr)
    {
        SetError(error, "Game module '" + artifact.string() + "' returned a null ABI descriptor.");
        CloseLibrary(handle);
        return {};
    }
    if (const std::optional<std::string> mismatch = DescribeGameModuleAbiMismatch(*moduleAbi, hostAbi))
    {
        SetError(error, "Game module ABI mismatch (" + *mismatch + ").");
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
