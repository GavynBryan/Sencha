#pragma once

#include <app/GameModule.h>
#include <app/GameModuleAbi.h>

#include <filesystem>
#include <optional>
#include <string>

class Game;

// First incompatible field between a module's reported ABI descriptor and this
// build's (SenchaThisBuildAbi()), as a human-readable reason — or nullopt when
// compatible. Exposed (not just used by the loader) so the guard itself is unit-
// testable. (09-module-abi-hardening.md.)
[[nodiscard]] std::optional<std::string> DescribeGameModuleAbiMismatch(
    const GameModuleAbi& module, const GameModuleAbi& host);

//=============================================================================
// GameModuleLoader
//
// Loads a game module artifact at runtime: open the library, resolve the single
// SenchaCreateGameModule factory, ABI-check, and return its Game. Keeps the
// library mapped until Unload. See docs/plans/sencha-level-editor/02-...md §3.
//
// Ownership rule (binding): the Game instance is owned by the MODULE (typically
// a function-local static). The host drives it (Engine::Run, or just
// OnRegisterComponents for an editor borrowing serializers) but never deletes it
// across the allocator boundary; teardown is the relevant Game hook then unmap.
//=============================================================================
struct LoadedModule
{
    void* Handle   = nullptr; // dlopen handle / HMODULE
    Game* Instance = nullptr; // module-owned Game (do not delete across the boundary)

    [[nodiscard]] bool IsValid() const { return Handle != nullptr && Instance != nullptr; }
};

class GameModuleLoader
{
public:
    // Returns an invalid LoadedModule on any failure (open, missing symbol, ABI
    // mismatch). *error, if provided, receives a human-readable reason. The host
    // decides what to do with the Game (run it, or only register components).
    LoadedModule Load(const std::filesystem::path& artifact, std::string* error = nullptr);

    // Unmap the library. Safe on an invalid module (no-op). The caller is
    // responsible for any Game teardown (OnShutdown / OnUnregisterComponents)
    // BEFORE this, while the module is still mapped.
    void Unload(LoadedModule& module);
};
