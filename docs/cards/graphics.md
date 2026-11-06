# graphics

Snapshot `97ed48e4` (2026-10-30). Layer: presentation.

## 1. Frame phases

- `AwaitFrameSlot`: registered at `engine/src/app/EngineFramePhases.cpp:1098`; waits for a free frame in flight.
- `RebuildGraphics`: registered at `engine/src/app/EngineFramePhases.cpp:1112`; rebuilds swapchains after resize or device events.
- `Render`: `DrawFrameScheduled` (`engine/src/app/EngineFramePhases.cpp:1178`) records every registered feature and presents.

## 2. Inputs

- Extracted render data from render, handed to features through `RenderFrame` (`engine/include/graphics/RenderFeature.h:72`).
- Window surfaces from platform, through `PresentationTarget` (`engine/include/graphics/vulkan/PresentationTarget.h:34`).
- Features from render (six) and debug (one), each an `IRenderFeature` (`engine/include/graphics/RenderFeature.h:90`).
- Module dependencies: assets, core, math, platform, profiling, render, runtime, time.

## 3. Outputs

- Presented frames on each window, through `Renderer` (`engine/include/graphics/vulkan/Renderer.h:148`).
- Frame statistics, written into profiling's `RenderStats` (`engine/include/profiling/RenderStats.h:16`): read by app, debug and render.
- Deferred GPU frees, `VulkanDeletionQueueService` (`engine/include/graphics/vulkan/VulkanDeletionQueueService.h:62`): used inside the Vulkan layer to free resources once the GPU is done with them.

## 4. Central decisions

- `Renderer::RecordSwapchainPhases` (`engine/src/graphics/vulkan/Renderer.cpp:590`): records each feature's phases into each presented target's command buffer.
- `VulkanFrameService::EndFrame` (`engine/src/graphics/vulkan/VulkanFrameService.cpp:422`): submits the frame and presents or skips each target.
- `PlanAcquisitions` (`engine/include/graphics/vulkan/PresentationFramePlan.h:36`): decides which windows acquire an image this frame.
- `UiDrawPass::Draw` (`engine/src/graphics/vulkan/UiDrawPass.cpp:322`): records the UI's extracted geometry.

## 5. Invariants

- A window takes part only when it can be shown: `PresentationFramePlan.AWindowTakesPartOnlyWhenItCanBeShown` (`test/runtime/PresentationFramePlanTests.cpp`).
- An acquire that times out skips only its own presentation: `PresentationFramePlan.AnAcquireThatTimesOutSkipsOnlyItsPresentation` (`test/runtime/PresentationFramePlanTests.cpp`).
- Staging order does not decide resolved feature order: `FeatureRegistrationOrder.StagingOrderDoesNotDecideResolvedOrder` (`test/runtime/FeatureRegistrationOrderTests.cpp`).
- An unknown feature dependency refuses the batch: `FeatureRegistrationOrder.AnUnknownDependencyRefusesTheBatch` (`test/runtime/FeatureRegistrationOrderTests.cpp`).
- A failed variant keeps nothing: `PipelineVariantSet.KeepsNothingWhenAVariantFailsToCompile` (`test/runtime/PipelineVariantSetTests.cpp`).
- Each frame gets its own scratch slice and a clean cursor: `FrameScratchRing.EachFrameGetsItsOwnSliceAndACleanCursor` (`test/core/FrameScratchRingTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 14,810 lines in 114 files.
- CCN 34 `UiDrawPass::Draw` (`engine/src/graphics/vulkan/UiDrawPass.cpp:322`); CCN 28 `VulkanFrameService::EndFrame` (`engine/src/graphics/vulkan/VulkanFrameService.cpp:422`); CCN 26 `Renderer::RecordSwapchainPhases` (`engine/src/graphics/vulkan/Renderer.cpp:590`).

## 7. Docs that contradict the code

- `docs/renderer/extending.md:386` says `FrameUniformRangeTests` fails when the block crosses the line; no such test exists.
- `docs/renderer/frame.md:168` describes `DisplayQueueTests`; no such test exists.
