#pragma once

#include <app/GameModule.h>

#include <filesystem>
#include <string>

//=============================================================================
// GameModuleLoader
//
// Loads a game module artifact at runtime: open the library, resolve the single
// SenchaCreateGameModule factory, ABI-check, and call Register(ctx). Keeps the
// library mapped until Unload. See docs/plans/sencha-level-editor/02-...md §3.
//
// Ownership rule (binding): the IGameModule instance and anything it registers
// are owned by the MODULE and torn down in Unregister; the engine's registries
// hold non-owning references for the module's lifetime. The host performs no
// cross-allocator delete. The factory therefore returns a module-owned instance
// (typically a function-local static), not something the host frees.
//=============================================================================
struct LoadedModule
{
    void*        Handle = nullptr; // dlopen handle / HMODULE
    IGameModule* Module = nullptr;

    [[nodiscard]] bool IsValid() const { return Handle != nullptr && Module != nullptr; }
};

class GameModuleLoader
{
public:
    // Returns an invalid LoadedModule on any failure (open, missing symbol, ABI
    // mismatch); on ABI mismatch Register is never called. *error, if provided,
    // receives a human-readable reason.
    LoadedModule Load(const std::filesystem::path& artifact,
                      GameModuleContext& ctx,
                      std::string* error = nullptr);

    // Unregister(ctx) then unmap. Safe on an invalid module (no-op).
    void Unload(LoadedModule& module, GameModuleContext& ctx);
};
