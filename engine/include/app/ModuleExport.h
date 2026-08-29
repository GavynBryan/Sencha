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
// IsRemovable hint on IComponentSerializer (a new trailing vtable slot). v6:
// removed Engine::Driver, Engine::ActiveRenderProfileMode, and the
// EngineFramePhases.h registration free function, and made the frame-phase
// accessors (Timing, Instrumentation, the profiling latch/publish pair)
// private; IRenderFeature lost the unreachable Contribute slot; JobSystem
// became the concrete pool and IWindow was removed. v7: the process-global
// component serializer registry is gone -- each host owns one and the scene
// save/load functions take it explicitly, so a module that reached for the
// default registry no longer links. Game::OnRegisterComponents is unchanged;
// the registry it receives is now the engine's own. v8: added the
// Game::OnRegisterDataAssetTypes / OnUnregisterDataAssetTypes pair, so a
// module can register structured data subtypes and their authoring schemas.
// v9: the fixed-tick, physics, post-fixed, render-extract, audio, and end-frame
// contexts no longer carry an InputFrame. Simulation reads resolved actions from
// InputActionState; PreSimulate (where the mapper runs) and FrameUpdate (editor
// and debug tooling) still expose the raw device frame. v10: participant
// lifecycle/control moved out of net into its own SDK domain, the old NetPlayer
// and NetParticipant surface was removed, and Engine now exposes typed
// participant outcomes through the session projection owner. v11: the render
// feature contract went neutral -- RenderPhase, IRenderFeature, and the new
// RenderFeatureServices / RenderFrame types moved to graphics/RenderFeature.h,
// and Setup/OnDraw take those instead of the backend RendererServices /
// FrameContext (still reachable through their Backend pointers). The
// fingerprint now covers graphics/*.h, the neutral shelf. v12: GpuFrameScratch
// allocations name their consumer -- the Allocate family takes a ScratchTag, so
// one slice's budget can be attributed per feature instead of only in aggregate.
// v13: Engine owns SceneSpawnService and exposes it as Spawns() -- runtime
// scene spawning over the cooked .smap pipeline; games wire their asset stack
// via ConnectAssets in OnStart. Engine gained a member, so accessor offsets
// moved.
#define SENCHA_GAME_ABI_VERSION 13u
