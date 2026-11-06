# platform

Snapshot `97ed48e4` (2026-10-30). Layer: foundation.

## 1. Frame phases

- `PumpPlatform`: SDL events are pumped and routed at `engine/src/app/EngineFramePhases.cpp:999`.
- `ResolveLifecycle`: window state is read at `engine/src/app/EngineFramePhases.cpp:1100` to settle resize and minimize.

## 2. Inputs

- SDL events and window handles, kept inside the `.cpp` files; `SdlWindow` exposes an opaque handle (`engine/include/platform/SdlWindow.h:17`).
- Window requests, `WindowCreateInfo` (`engine/include/platform/WindowCreateInfo.h:7`).
- Module dependencies: core.

## 3. Outputs

- `PlatformServices` (`engine/include/platform/PlatformServices.h:20`): window and video services consumed by app, input, graphics, ui and debug.
- Window state per window, through `SdlWindowService` (`engine/include/platform/SdlWindowService.h:19`): consumed by the frame loop's lifecycle phase and by presentation targets.

## 4. Central decisions

- `ClassifyWindowFrameHit` (`engine/src/platform/WindowFrameHit.cpp:12`): decides whether a pointer is on a self-drawn window's caption, an edge, a corner or the client area.
- `SdlEventWindowId` (`engine/src/platform/SdlWindowService.cpp:10`): decides which window an SDL event belongs to.

## 5. Invariants

- An edge wins over the caption: `WindowFrameHit.EdgeWinsOverCaption` (`test/core/WindowFrameHitTests.cpp`).
- A held pointer is client everywhere: `WindowFrameHit.AHeldPointerIsClientEverywhere` (`test/core/WindowFrameHitTests.cpp`).
- Degenerate inputs classify as client: `WindowFrameHit.DegenerateInputsAreClient` (`test/core/WindowFrameHitTests.cpp`).
- Resizing one window leaves the others rendering: `PresentationTest.ResizingOneWindowLeavesTheOthersRendering` (`test/presentation/PresentationTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 1,074 lines in 16 files.
- CCN 27 `ClassifyWindowFrameHit` (`engine/src/platform/WindowFrameHit.cpp:12`); CCN 21 `SdlEventWindowId` (`engine/src/platform/SdlWindowService.cpp:10`); CCN 13 `FrameHitTest` (`engine/src/platform/SdlWindow.cpp:49`).

## 7. Docs that contradict the code

- None found.
