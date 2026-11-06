# render

Snapshot `97ed48e4` (2026-10-30). Layer: presentation.

## 1. Frame phases

- `Update` (FrameUpdate lane): `RunFrameUpdate` (`engine/src/app/EngineFramePhases.cpp:845`) runs `MeshOverrideSystem` (`engine/src/app/Engine.cpp:181`) when a window is presented.
- `ExtractRender`: `RunExtractRender` (`engine/src/app/EngineFramePhases.cpp:1151`) reaches `DefaultRenderPipeline::ExtractRender` (`engine/src/app/DefaultRenderPipeline.cpp:211`), which runs the extractors in `engine/include/render/extract/`.
- `Render`: render features and passes record through `DrawFrameScheduled` (`engine/src/app/EngineFramePhases.cpp:1178`), owned by graphics.

## 2. Inputs

- Mesh, light and camera components in the visible partitions, read by `RenderExtractionSystem` (`engine/include/render/extract/RenderExtractionSystem.h:118`), `LightExtractionSystem` (`engine/include/render/extract/LightExtractionSystem.h:34`) and `ShadowCasterExtractionSystem` (`engine/include/render/extract/ShadowCasterExtractionSystem.h:72`).
- Resident materials and meshes, `MaterialCache` (`engine/include/render/MaterialCache.h:34`), from assets.
- The presented view, `PresentedFrame` (`engine/include/camera/PresentedFrame.h:18`).
- Module dependencies: anim, app, assets, camera, components, core, debug, ecs, graphics, math, presentation, profiling, time, ui, world.

## 3. Outputs

- Draw items, `RenderQueueItem` (`engine/include/render/RenderQueue.h:60`), keyed by `BuildOpaqueSortKey` (`engine/include/render/RenderQueue.h:95`): consumed by the mesh forward pass.
- Selected forward lights, `RenderLightSet` (`engine/include/render/RenderLight.h:11`): consumed by light bindings and shadow scheduling.
- Shadow caster changes, `ShadowCasterSet` (`engine/include/render/ShadowCasterSet.h:65`): consumed by the shadow depth pass.
- Frame graph order, as `FrameView` records (`engine/include/render/FrameComposition.h:101`): consumed inside render when it records.

## 4. Central decisions

- `FrameComposition::Resolve` (`engine/src/render/FrameComposition.cpp:138`): orders views and work by their dependencies and skips what cannot run.
- `ComposeMeshOverrides` (`engine/src/render/MeshOverrides.cpp:104`): resolves competing material and visibility overrides on a mesh by writer precedence.
- `ScheduleShadowSubViews` (`engine/src/render/ShadowSlotPool.cpp:80`): decides which shadow views render this frame within the atlas budget.
- `SelectForwardLights` (`engine/include/render/LightSelection.h:50`): ranks lights against the view and packs the brightest under the cap.

## 5. Invariants

- A dependent runs after its producer no matter when it was declared: `FrameComposition.ADependentRunsAfterItsProducerNoMatterWhenItWasDeclared` (`test/runtime/FrameCompositionTests.cpp`).
- Skipping a producer skips everything downstream of it: `FrameComposition.SkippingAProducerSkipsEverythingDownstreamOfIt` (`test/runtime/FrameCompositionTests.cpp`).
- Light score ties break on entity key, so order is deterministic: `SelectForwardLights.BreaksScoreTiesOnEntityKeySoTheOrderIsDeterministic` (`test/runtime/LightSelectionTests.cpp`).
- Partition-set order does not change tie breaks: `LightExtraction.PartitionSetOrderDoesNotChangeTieBreaks` (`test/runtime/LightExtractionTests.cpp`).
- Lights outside the active partitions are skipped: `LightExtraction.SkipsLightsOutsideTheActivePartitions` (`test/runtime/LightExtractionTests.cpp`).
- A section whose material is not resident is skipped: `EmitMeshSections.SkipsASectionWhoseMaterialIsNotResident` (`test/runtime/MeshDrawInstanceTests.cpp`).

### Unchecked

- That backends see only extracted data: render passes include Vulkan directly (`engine/src/render/pass/MeshForwardPass.cpp`), and no fitness test checks the render/graphics boundary in either direction.

## 6. Size

- 11,288 lines in 91 files.
- CCN 27 `FrameComposition::Resolve` (`engine/src/render/FrameComposition.cpp:138`); CCN 23 `ComposeMeshOverrides` (`engine/src/render/MeshOverrides.cpp:104`); CCN 22 `ScheduleShadowSubViews` (`engine/src/render/ShadowSlotPool.cpp:80`).

## 7. Docs that contradict the code

- `docs/core-systems-map.md:47` lists `render/RenderExtractionSystem.cpp`; the file is at `engine/src/render/extract/RenderExtractionSystem.cpp`.
- `docs/renderer/architecture.md:107` names `ImGuiTextureBinding`; no such symbol exists in the tree.
- `docs/renderer/baked-lighting.md:122` names `AddZoneVolumes` and `ReleaseZone` as the owner-thread entry points; neither exists in the tree.
- `docs/building.md:249` blames `include/render/static_mesh/GpuStaticMesh.h` for the Vulkan-off build failure; that file does not exist (the Vulkan includes are under `engine/src/render/pass`).
