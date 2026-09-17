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
// moved. v14: added Game::OnRegisterVocabulary (a new trailing vtable slot), so
// a module declares its gameplay tags, attributes, abilities, and locomotion
// modes into any World -- the runtime's, and each of the editor's authoring
// documents. v15: the engine owns the process's content. Engine gained a
// RuntimeContent member (so accessor offsets moved) and exposes it as Content();
// it composes the asset stack, mounts RuntimeConfig::ContentRoots, publishes the
// world's asset resources, and connects the spawn services and render pipeline
// itself. A module no longer builds a RuntimeAssets, and
// Game::OnRegisterDataAssetTypes is now called by the runtime rather than only
// by the data editor. v16: the engine loads levels. Engine gained a LoadedLevel
// member (so accessor offsets moved) and exposes it as Level(); map, world,
// zone, zones, scene.spawn and scene.despawn are engine console commands, and
// ConsoleService::SetMapHandler is gone -- `+map` loads through the engine
// rather than calling back into a game. A game learns that a level arrived from
// the zone-residency changes the load publishes. v17: participant admission is
// session-gated. SessionParticipantProjection::AdmitLocal, AdmitSimulated and
// RequestBody take whether a session is active and add replication state only
// then; Engine::ProjectSessionStart stamps retroactively when a process starts
// hosting. SetLocalControlSubject publishes identity only and no longer adds or
// removes LocalLookControl -- a game that wants that rule composes it. v18:
// the engine has no camera policy and no local streaming policy. CameraRig,
// CameraRigMode, ComputeCameraPose, CameraFollowSystem, RegisterCameraSystem and
// the authored CameraSeat (CSET) are gone; CameraExclusion is the one runtime
// camera component, read by extraction, and FirstAuthoredCamera is the one
// query. NetZoneStreaming::Update no longer takes a local control subject; a game
// sets the partition's primary focus itself. v19: RegisterCameraComponents(World&)
// is gone -- the engine schema gives every world storage for its components, so
// the call registered nothing. Render extraction and presentation-domain
// transform propagation take the frame's interpolation alpha: a camera with pose
// history, or one parented to an entity with it, is drawn from the blend the
// meshes are. v20: app/BodySpawns owns a prefab body request from
// the ask until its group is handed to the participant lifecycle or cleaned
// up; games install it as their body policy instead of keeping the book
// themselves. SceneSpawnService::IsDespawnRequested reports an ended request
// before the pump, and AsyncTaskQueue::Stop ends the cross-frame lane. The
// engine stops that lane right after Game::OnShutdown returns, so a module may
// not submit async work from its shutdown hook or later. v21: the authored UI
// layer reaches modules. Engine::Ui() hands back a UiService, and a game opens
// screens, publishes presentation values and drains semantic actions through
// it; the engine drives update, extraction and rendering, and the ui/ headers
// join the ABI fingerprint because a module holds those handles and values by
// value. A screen holds asset leases, so it lives inside the content stack's
// span: the engine shuts the UI down right after Game::OnShutdown returns, and
// a module must not hold a screen past its own shutdown hook. v22: the authored
// UI facade settles. UiRow carries an Editable flag, so a row says whether the
// document should offer a control for its value -- which changes the type's
// layout, and a module passes rows by value. UiService::DrainActions takes a
// screen, so a host with more than one authored controller stops having them
// swallow each other's actions; the no-argument form stays for a host that owns
// every screen. With the control vocabulary and two real surfaces built on it,
// the facade is frozen: it changes from here under the ordinary rules rather
// than a stage at a time. v23: the application shell. Engine carries the saved
// settings store, so its size changes and a module holding an Engine& has to be
// rebuilt; EngineRuntimeConfig loses ExitOnEscape and TogglePauseOnF1, whose
// raw scancode handlers the shell's rebindable Back action replaces, which
// changes EngineConfig's layout and a module receives one by reference in every
// lifecycle context. RuntimeFrameLoop gains a suspension flag separate from the
// timescale, and InputContextDefinition a shell flag, both of which a module
// sees by value through the headers it already includes. In the same bump:
// EngineRuntimeConfig gains ApplicationShell and EngineConsoleConfig gains
// SettingsRoot, so a host declares its posture and where settings may live;
// EngineDebugConfig loses DebugUi, which nothing read; and UiRow carries
// control metadata (Control, Min, Max, Step, Choices) beside Editable, so a
// row can name the slider or drop-down a document offers for it.
#define SENCHA_GAME_ABI_VERSION 23u
