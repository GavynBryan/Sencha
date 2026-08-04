# The Frame

## Phase placement

The renderer occupies two of the eleven `FramePhase` slots
(`engine/include/runtime/FrameDriver.h`). Registration for both lives in
`engine/src/app/EngineFramePhases.cpp`.

| Phase | Renderer work |
|---|---|
| `RebuildGraphics` (2) | `VulkanSwapchainService::Recreate`, `VulkanFrameService::ResetAfterSwapchainRecreate`, `Renderer::NotifySwapchainRecreated` |
| `ExtractRenderPacket` (8) | latch the profile mode, propagate visible transforms, run every registered extract system (`DefaultRenderPipeline::ExtractRender`) |
| `Render` (9) | `Renderer::DrawFrameScheduled`, then push the timing sample and the stats frame |

Lifecycle-only frames (resize, minimize, swapchain rebuild) skip extract and
render but still pump platform events and stamp telemetry.

## Extract

`DefaultRenderPipeline::ExtractRender` runs in this fixed order. Each numbered
step is wrapped in the `CpuScope` named beside it, so the profile panel and the
capture attribute them separately.

1. **Camera.** Walk `ctx.ActiveRegistries` for the first registry that has an
   `ActiveCameraService` resource plus `CameraComponent` and `WorldTransform`
   registered, and build `CameraRenderData` from it against the current
   swapchain extent. No camera means `PacketWrite.Renderable = false` and an
   early return: nothing else runs.
2. **`CpuScope::Extraction`.** `RenderQueue::Reset`, then
   `RenderExtractionSystem::Extract` once per active registry, then
   `RenderQueue::SortOpaque`.
3. **`CpuScope::LightSelection`.** `RenderLightSet::Reset`, apply the `render.*`
   cvars onto the light set, `LightExtractionSystem::Extract` (which internally
   calls `SelectForwardLights`), then `ProbeVolumeSet::AppendActive`.
4. **`CpuScope::ShadowGather`.** Ask the arbiter whether any live slot uses the
   `OnChange` policy. Extract the caster set, building the per-entity record
   table only if the answer was yes, then run `ShadowCasterDiff::Apply` to
   produce this frame's events.
5. **`CpuScope::ShadowResidency`.** `ShadowResidency::Update` with the requests,
   the events, and the budgets read from cvars, then `ApplyGrants` to stamp
   shadow indices and slot records onto the light set.
6. Publish extraction counters into `RenderStats` if the mode is Counters or
   above, and warn once when the 64-light cap actually dropped a candidate.

The caster diff has a reseed rule worth knowing: a frame that did not build
records leaves the retained table older than the gap, so the first frame after
such a gap adopts the current set as the baseline instead of reporting every
caster as new. `CasterRecordsWereBuilt` carries that state.

## Record and present

`Renderer::DrawFrameScheduled` (`engine/src/graphics/vulkan/Renderer.cpp`) is
the whole submission path.

```
DrawFrameScheduled
  VulkanFrameService::BeginFrame
    wait on this slot's in-flight fence (if it was submitted)
    VulkanDeletionQueueService::AdvanceFrame
    vkWaitForPresentKHR on this slot's previous presentId   [if present_wait]
    vkAcquireNextImageKHR                                    [signals ImageAvailable]
    wait on the acquired image's last-recorded fence         [if same generation]
    vkResetCommandPool + vkBeginCommandBuffer
  VulkanFrameScratch::BeginFrame                             [rotate slice, reset cursor]
  GpuTimestampPool::BeginFrame                               [collect previous, reset queries]
  RecordOffscreenPhase                                       [GpuScope::PhaseOffscreen]
    for each Offscreen feature: OnDraw   (features own their own rendering scopes)
  RecordMainColorPhase                                       [GpuScope::PhaseMainColor]
    barrier: swapchain image -> COLOR_ATTACHMENT_OPTIMAL
    depth target Recreate(extent) + depth barrier
    vkCmdBeginRendering (color clear, depth clear, depth storeOp DONT_CARE)
    for each MainColor feature: OnDraw
    vkCmdEndRendering
    barrier: swapchain image -> PRESENT_SRC_KHR
  publish scratch counters into RenderStats
  VulkanFrameService::EndFrame
    vkEndCommandBuffer
    vkResetFences + vkQueueSubmit                            [waits ImageAvailable, signals per-image RenderFinished]
    record this image's in-flight fence
    vkQueuePresentKHR                                        [waits per-image RenderFinished, carries presentId]
    advance CurrentFrame
```

The `Offscreen` bucket is skipped entirely when empty, which is the game's
normal case only when the shadow feature failed setup. Offscreen features open
and close their own rendering scopes and own their own image barriers; no
swapchain rendering scope is open around them.

## Synchronization objects

| Object | Count | Signalled by | Waited by |
|---|---|---|---|
| `ImageAvailable` semaphore | one per frame in flight | `vkAcquireNextImageKHR` | the frame's `vkQueueSubmit`, at `ALL_COMMANDS` |
| `RenderFinished` semaphore | one per **swapchain image** | that frame's submit | `vkQueuePresentKHR` |
| `InFlightFence` | one per frame in flight | that frame's submit | next `BeginFrame` for the same slot, and by any frame acquiring the same image |
| `presentId` | monotonic per present | `VK_KHR_present_id` | `vkWaitForPresentKHR` at the next `BeginFrame` for that slot |

`RenderFinished` is per image, not per frame slot. A per-slot signal semaphore
would be waited by a present for an image another slot is still using when the
swapchain has more images than frames in flight.

`ImageInFlightFences` is stamped with the swapchain generation. After a
recreate, the recorded fences describe a dead chain, so the generation check
skips the wait rather than blocking on a fence that will never be relevant.

## Pacing

Two mechanisms, in order of preference:

1. **`VK_KHR_present_wait`.** Requested as an optional device extension along
   with `VK_KHR_present_id` (`GraphicsServices::BuildPolicy`). When present,
   `BeginFrame` blocks on the presentation of this slot's previous frame. That
   is the true vsync anchor: without it the GPU queues ahead and the frame
   cadence goes lumpy.
2. **Acquire-based pacing.** The fallback when the extension is absent
   (macOS/MoltenVK, older drivers). `ChooseImageCount` therefore requests
   `minImageCount` exactly, usually 2, so `vkAcquireNextImageKHR` itself blocks
   on vsync. Requesting `minImageCount + 1` creates a third image that lets the
   GPU queue an extra frame and produces missed-vsync microstutter.

`vkWaitForPresentKHR` is skipped when the recorded presentId belongs to a
retired swapchain generation, because waiting on a dead swapchain is undefined.

Present mode selection: FIFO by default, overridable per process with the
`SENCHA_PRESENT_MODE` environment variable (`IMMEDIATE`, `MAILBOX`, `FIFO`,
`FIFO_RELAXED`). Benchmarks force `IMMEDIATE` so measured frame times reflect
work and not the vsync interval.

## Swapchain lifecycle

`RenderFrameResult` is the renderer's report to `RuntimeFrameLoop`. It exists so
surface instability never leaks into game time.

| Result | Meaning | Frame loop reaction |
|---|---|---|
| `Presented` | normal | nothing |
| `SwapchainOutOfDate` | acquire or present returned `VK_ERROR_OUT_OF_DATE_KHR` | set surface extent, mark swapchain invalidated |
| `SurfaceSuboptimal` | acquire or present returned `VK_SUBOPTIMAL_KHR` | same as out of date |
| `SkippedMinimized` | swapchain invalid or zero images | nothing; lifecycle-only frames continue |
| `Failed` | device lost or an unrecoverable Vulkan error | request quit |

Recreation happens in the `RebuildGraphics` phase, never mid-frame:

```
VulkanSwapchainService::Recreate(extent)
  vkDeviceWaitIdle
  destroy views + images of the outgoing chain, keep its handle
  vkCreateSwapchainKHR(oldSwapchain = outgoing)     [driver may reuse resources]
  vkDestroySwapchainKHR(outgoing)                   [retired by the create either way]
  ++Generation, ++RecreateCount
VulkanFrameService::ResetAfterSwapchainRecreate
  recreate per-image semaphores, clear per-slot submitted/presentId state
  (no second device idle: Recreate already idled)
Renderer::NotifySwapchainRecreated
  reset the tracked per-image layouts and the depth layout to UNDEFINED
```

The depth target is recreated inside `RecordMainColorPhase` instead, because it
follows the swapchain extent and `VulkanDepthTarget::Recreate` is a no-op when
the extent has not changed.

## Timing sample

After `DrawFrameScheduled` returns, the `Render` phase pushes one
`TimingFrameSample` through `TimingSampler::PushRenderFrame`, carrying:

- `RendererFrameTiming`: seconds spent recording, seconds for the whole call.
- `VulkanFrameTiming`: acquire, submit, present, and present-wait seconds, plus
  image index and swapchain generation.
- `SwapchainState`: extent, format, color space, present mode, image counts,
  generation, and the cumulative recreate count.
- The `RenderFrameResult`.
- The last collected GPU scope spans and this frame's CPU scope milliseconds,
  when instrumentation is active.

Then `Engine::PushRenderStatsFrame` appends the frame's `RenderStats` to the
history ring and, in Capture mode, to the capture ring.
