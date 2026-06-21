#pragma once

#include <cstdint>

//=============================================================================
// Game-module ABI export macros + version.
//
// Engine and game modules build with -fvisibility=hidden (the hostile posture
// the S0 spike validated); only the single factory symbol and the engine's
// intentionally-public ABI surface are exported. See
// docs/plans/sencha-level-editor/02-...md §2.2.
//=============================================================================
#if defined(_WIN32)
  #define SENCHA_GAME_EXPORT __declspec(dllexport)
  #define SENCHA_GAME_IMPORT __declspec(dllimport)
#else
  #define SENCHA_GAME_EXPORT __attribute__((visibility("default")))
  #define SENCHA_GAME_IMPORT
#endif

// Bumped on a *deliberate* break of the game-module contract / registration
// surface. Most skew is now caught automatically by the ABI fingerprint (a hash
// of the module-facing headers) plus the build-identity record in
// GameModuleAbi.h; this integer remains the human-meaningful "intended break"
// marker. v3: added the GameModuleAbi handshake and the EditorVisual hint on
// IComponentSerializer. v4: the module factory returns a Game (retiring the
// IGameModule/GameModuleContext side-contract); serializer registration is the
// Game::OnRegisterComponents hook. (09-module-abi-hardening.md.) v5: added the
// IsRemovable hint on IComponentSerializer (a new trailing vtable slot).
#define SENCHA_GAME_ABI_VERSION 5u
