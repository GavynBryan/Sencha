# camera

Snapshot `97ed48e4` (2026-10-30). Layer: motion and space.

## 1. Frame phases

- `Update`: `RunPresentationPhase` (`engine/src/app/EngineFramePhases.cpp:855`) calls `PublishPresentedFrame` (`engine/src/app/PresentationPhase.cpp:15`) before the PresentationUpdate lane.
- The module schedules no system; shake and field-of-view effects apply inside `PresentCameraEffects` (`engine/src/camera/PresentedFrame.cpp:65`).

## 2. Inputs

- The active camera, read from `ActiveCameraService` at `ResolvePresentedFrame` (`engine/src/camera/PresentedFrame.cpp:10`).
- Effect state, `CameraShakeState` (`engine/include/camera/CameraShake.h:51`) and `CameraFovScaleState` (`engine/include/camera/CameraFovScale.h:31`), started by verbs.
- Authored shake profiles, `CompileCameraShakeProfile` (`engine/include/camera/CameraShakeProfile.h:50`).
- Module dependencies: assets, authored, components, core, ecs, graphics, math, render, time, world.

## 3. Outputs

- `PresentedFrame` (`engine/include/camera/PresentedFrame.h:18`): the view this frame is drawn with, consumed by the PresentationUpdate lane and render extraction (`engine/src/render/extract/Camera.cpp`).
- `ProjectWorldPoint` (`engine/include/camera/PresentedFrame.h:59`): screen placement for world points, for HUD markers.
- `CameraExclusion` (`engine/include/camera/CameraExclusion.h:20`): entities hidden from their owner's view, consumed by render extraction.

## 4. Central decisions

- `ResolvePresentedFrame` (`engine/src/camera/PresentedFrame.cpp:10`): picks the camera and builds the view and projection for this frame.
- `CompileCameraShakeProfile` (`engine/src/camera/CameraShakeProfile.cpp:144`): accepts or refuses an authored shake.
- `CameraShakeOperations::Start` (`engine/src/camera/CameraShakeVerbs.cpp:70`): admits a shake request from its arguments.

## 5. Invariants

- Noise is a function of time and seed alone: `CameraShake.NoiseIsAFunctionOfTimeAndSeedAlone` (`test/runtime/CameraEffectTests.cpp`).
- The same instant shakes the same at any frame rate: `CameraShake.TheSameInstantShakesTheSameAtAnyFrameRate` (`test/runtime/CameraEffectTests.cpp`).
- Many shakes together stay inside the limits: `CameraShake.ManyShakesTogetherStayInsideTheLimits` (`test/runtime/CameraEffectTests.cpp`).
- The rendered view moves while the camera stays put: `CameraShake.TheRenderedViewMovesWhileTheCameraStaysPut` (`test/runtime/CameraEffectTests.cpp`).
- A point in front but off the frame is outside, not behind: `WorldPointProjection.InFrontButOffTheFrameIsOutsideNotBehind` (`test/runtime/PresentedFrameTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 1,428 lines in 26 files.
- CCN 21 `CompileCameraShakeProfile` (`engine/src/camera/CameraShakeProfile.cpp:144`); CCN 14 `CameraFovScaleOperations::Start` (`engine/src/camera/CameraFovScaleVerbs.cpp:82`); CCN 14 `CameraShakeOperations::Start` (`engine/src/camera/CameraShakeVerbs.cpp:70`).

## 7. Docs that contradict the code

- None found.
