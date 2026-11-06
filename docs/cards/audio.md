# audio

Snapshot `97ed48e4` (2026-10-30). Layer: presentation.

## 1. Frame phases

- `Update` (Audio lane): `RunAudio` (`engine/src/app/EngineFramePhases.cpp:914`) runs `AudioSystem` (`engine/src/app/Engine.cpp:190`) and then `CaptionSystem` (`engine/src/app/Engine.cpp:193`).

## 2. Inputs

- Emitters, `AudioSourceComponent` (`engine/include/audio/AudioSourceComponent.h:29`), and captions, `AudioCaptionComponent` (`engine/include/audio/AudioCaptionComponent.h:24`), in active zones.
- Audio and caption settings from engine config; playback is turned off when no window is presented (`engine/src/app/Engine.cpp:187`).
- Module dependencies: app, authored, core, ecs, world.

## 3. Outputs

- Playing voices on the SDL device, through `AudioService` (`engine/include/audio/AudioService.h:61`).
- Visible captions, `CaptionRuntime` (`engine/include/audio/CaptionRuntime.h:43`): consumed by app.

## 4. Central decisions

- `AudioSystem::Update` (`engine/src/audio/AudioSystem.cpp:20`): starts, restarts and stops voices as sources enter, leave and return to active zones.
- `CaptionSystem::Update` (`engine/src/audio/CaptionSystem.cpp:66`): starts, degrades and retires captions alongside their sources.
- `CaptionRuntime::BeginInternal` (`engine/src/audio/CaptionRuntime.cpp:152`): admits, merges or trims a caption by priority and recency.

## 5. Invariants

- A loop restarts across dormancy and a one-shot does not: `AudioSystemSweep.LoopRestartsAcrossDormancyAndOneShotDoesNot` (`test/runtime/AudioRuntimeTests.cpp`).
- Removal stops the voice before releasing the sole clip reference: `AudioSourceLifetime.RemoveStopsVoiceBeforeReleasingSoleClipReference` (`test/runtime/AudioRuntimeTests.cpp`).
- Disabled playback opens no device: `AudioService.PlaybackDisabledOpensNoDevice` (`test/runtime/AudioRuntimeTests.cpp`).
- Priority-then-recency trim is deterministic: `CaptionPolicy.PriorityThenRecencyTrimIsDeterministic` (`test/runtime/CaptionRuntimeTests.cpp`).
- A rejected scene subtitle degrades to timed and closed captions drop: `CaptionSystemDegrade.RejectedSceneSubtitleDegradesToTimedAndCCDrops` (`test/runtime/CaptionSystemTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 2,540 lines in 26 files.
- CCN 24 `AudioSystem::Update` (`engine/src/audio/AudioSystem.cpp:20`); CCN 15 `CaptionSystem::Update` (`engine/src/audio/CaptionSystem.cpp:66`); CCN 14 `CaptionRuntime::BeginInternal` (`engine/src/audio/CaptionRuntime.cpp:152`).

## 7. Docs that contradict the code

- `docs/audio/runtime.md:76` says saving goes through `GetPathForAudioClip`; no such symbol exists in the tree.
- `docs/audio/runtime.md:79` names `EngineSceneComponents`; no such symbol exists in the tree.
