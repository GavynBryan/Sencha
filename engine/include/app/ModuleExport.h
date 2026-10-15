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

// Bumped on a deliberate break of the game-module contract; the ABI fingerprint
// catches the rest. What each version changed: docs/architecture/module-abi-history.md.
#define SENCHA_GAME_ABI_VERSION 27u
