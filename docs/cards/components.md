# components

Snapshot `97ed48e4` (2026-10-30). Layer: foundation.

## 1. Frame phases

- None of its own: two data types read during `Update` presentation and `ExtractRender` by camera and render code.

## 2. Inputs

- Authored camera values in scenes, the fields of `CameraComponent` (`engine/include/components/CameraComponent.h:44`).
- The active camera choice, set through `ActiveCameraService::SetActive` (`engine/include/components/ActiveCameraService.h:16`).
- Module dependencies: core, ecs, math.

## 3. Outputs

- `CameraComponent` (`engine/include/components/CameraComponent.h:44`) with `ProjectionKind` (`engine/include/components/CameraComponent.h:20`): read by camera placement and render extraction.
- `ActiveCameraService` (`engine/include/components/ActiveCameraService.h:13`), a World resource added at `engine/src/world/RuntimeWorld.cpp:26`: read by `engine/src/app/DefaultRenderPipeline.cpp:230` and `engine/src/camera/PresentedFrame.cpp:62`.

## 4. Central decisions

- `ActiveCameraService::SetActive` (`engine/include/components/ActiveCameraService.h:16`): records which entity is the view; every consumer reads the same choice.

## 5. Invariants

- A dead active camera builds no frame: `CameraRenderDataExclusion.ADeadActiveCameraBuildsNothing` (`test/runtime/CameraRenderDataTests.cpp`).
- One registration fills every registry consistently: `RuntimeComponentSchema.OneRegistrationFillsEveryRegistryConsistently` (`test/core/RuntimeComponentSchemaStartupTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 90 lines in 2 files, all headers.
- No function above CCN 1; the largest is the `CameraComponent` declaration block (`engine/include/components/CameraComponent.h:43`).

## 7. Docs that contradict the code

- None found.
