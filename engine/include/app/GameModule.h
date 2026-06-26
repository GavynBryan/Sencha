#pragma once

#include <app/GameModuleAbi.h>
#include <app/ModuleExport.h>

class Game;

//=============================================================================
// Game module entry point.
//
// A game module (game.so / game.dll) exposes exactly ONE C-linkage factory,
// SenchaCreateGameModule(), returning a Game created INSIDE the module. The
// Game's vtable crosses the boundary the same way IComponentSerializer's does;
// its lifecycle hooks run in the module against the host engine reached via
// Game::GetEngine(). Serializers and cvars register through the normal engine
// functions and Game hooks (the engine is one shared library, so its global
// registries are a single instance the module shares) -- there is no separate
// registration side-object.
//
// Ownership (binding): the returned Game is MODULE-owned (typically a
// function-local static). The host never deletes it across the allocator
// boundary; teardown is Game::OnShutdown (or OnUnregisterComponents for an
// editor that only borrowed serializers) followed by unmapping the library.
//=============================================================================

// The ONLY exported symbol of a game module (besides its ABI descriptor). C
// linkage: resolved by a fixed name, no C++ mangling across the boundary.
extern "C" SENCHA_GAME_EXPORT Game* SenchaCreateGameModule();
