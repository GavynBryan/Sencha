# The Frame

## Phase placement

The renderer occupies two of the eleven `FramePhase` slots
(`engine/include/runtime/FrameDriver.h`). Registration for both lives in
`engine/src/app/EngineFramePhases.cpp`.

| Phase | Renderer work |
|---|---|
| `RebuildGraphics` (2) | `VulkanFrameService::RebuildPresentation` for the primary when the frame loop says so, then `RebuildStaleSecondaries` |
| `ExtractRender` (10) | latch the profile mode, propagate visible transforms, run every registered extract system (`DefaultRenderPipeline::ExtractRender`) |
| `Render` (11) | `Renderer::DrawFrameScheduled`, then push the timing sample and the stats frame |

Lifecycle-only frames (resize, minimize, swapchain rebuild) skip extract and
render but still pump platform events and stamp telemetry. A minimized primary
makes a frame lifecycle-only only when no other presentation can be shown
(`RuntimeFrameLoop::SetOtherPresentationLive`).

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
    destroy retired presentations, then run their callbacks
    vkWaitForPresentKHR on the primary's previous presentId  [if present_wait]
    for each acquirable presentation:
      vkAcquireNextImageKHR                                  [signals its ImageAvailable]
      wait on the acquired image's last-recorded fence
    nothing acquired -> NothingAcquired, no recording
    vkResetCommandPool + vkBeginCommandBuffer
  GpuFrameScratch::BeginFrame                                [rotate slice, reset cursor]
  GpuTimestampPool::BeginFrame                               [collect previous, reset queries]
  RecordOffscreenPhase                                       [GpuScope::PhaseOffscreen]
    for each Offscreen feature: OnDraw   (Global ones always; presentation-scoped
                                          ones only if their presentation was acquired)
  for each acquired presentation: RecordSwapchainPhases     [phase GpuScopes for the primary]
    barrier: swapchain image -> COLOR_ATTACHMENT_OPTIMAL
    its depth attachment Recreate(extent) + barrier          [if declared]
    vkCmdBeginRendering (color clear, depth clear, depth storeOp DONT_CARE)
    for each swapchain phase, each feature scoped to this presentation: OnDraw
    vkCmdEndRendering
    capture, if armed for this presentation
    barrier: swapchain image -> PRESENT_SRC_KHR
  publish scratch counters into RenderStats
  VulkanFrameService::EndFrame
    vkEndCommandBuffer
    vkResetFences + vkQueueSubmit     [waits every acquired ImageAvailable, signals each image's RenderFinished]
    record each image's in-flight fence
    vkQueuePresentKHR over every acquired swapchain          [pResults per swapchain; presentId on the primary]
    advance CurrentFrame
```

The `Offscreen` bucket is skipped entirely when empty, which is the game's
normal case only when the shadow feature failed setup. Offscreen features open
and close their own rendering scopes and own their own image barriers; no
swapchain rendering scope is open around them.

## Synchronization objects

| Object | Count | Signalled by | Waited by |
|---|---|---|---|
| `ImageAvailable` semaphore | one per frame in flight, per presentation | `vkAcquireNextImageKHR` | the frame's `vkQueueSubmit`, at `ALL_COMMANDS` |
| `RenderFinished` semaphore | one per **swapchain image**, per presentation | that frame's submit | `vkQueuePresentKHR` |
| `InFlightFence` | one per frame in flight | that frame's submit | next `BeginFrame` for the same slot, and by any frame acquiring an image it last rendered |
| `presentId` | monotonic, primary only | `VK_KHR_present_id` | `vkWaitForPresentKHR` at the next `BeginFrame` for that slot |

`RenderFinished` is per image, not per frame slot. A per-slot signal semaphore
would be waited by a present for an image another slot is still using when the
swapchain has more images than frames in flight.

Per-image state lives on the `PresentationTarget` and is dropped when its
swapchain is rebuilt, so a recorded fence never describes a dead chain.

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
surface instability never leaks into game time. It speaks for the primary
presentation, whose resize lifecycle the loop owns; a secondary that goes out
of date is marked on its own target and rebuilt at the next `RebuildGraphics`
without colouring the frame (`SummarizeFrame`).

| Result | Meaning | Frame loop reaction |
|---|---|---|
| `Presented` | normal | nothing |
| `SwapchainOutOfDate` | acquire or present returned `VK_ERROR_OUT_OF_DATE_KHR` | set surface extent, mark swapchain invalidated |
| `SurfaceSuboptimal` | acquire or present returned `VK_SUBOPTIMAL_KHR` | same as out of date |
| `SkippedMinimized` | no presentation could be acquired | nothing; lifecycle-only frames continue |
| `Failed` | device lost or an unrecoverable Vulkan error | request quit |

Recreation happens in the `RebuildGraphics` phase, never mid-frame:

```
PresentationTarget::Rebuild(extent)
  VulkanSwapchainService::Recreate(extent)
    vkDeviceWaitIdle
    destroy views + images of the outgoing chain, keep its handle
    vkCreateSwapchainKHR(oldSwapchain = outgoing)   [driver may reuse resources]
    vkDestroySwapchainKHR(outgoing)                 [retired by the create either way]
    ++Generation, ++RecreateCount
  recreate per-image semaphores and layouts, clear presentIds and the depth layout
  (no second device idle: Recreate already idled)
```

The depth attachment is recreated inside `RecordSwapchainPhases` instead,
because it follows the swapchain extent and `VulkanDepthTarget::Recreate` is a
no-op when the extent has not changed.

## Presentations

One `Renderer` drives any number of peer presentations, one per window
(`graphics/vulkan/PresentationTarget.h`). The primary is the one created with
`GraphicsServices`; it is the default target for a swapchain-phase feature and
the one frame pacing follows, and nothing else about it is special.

- **Identity.** `PresentationId` is a generational handle held in a
  `HandlePool`, so an id kept past its presentation's destruction resolves to
  nothing, never to the presentation created after it.
- **Per-target state.** Each target owns its surface, swapchain, acquire and
  render-finished semaphores, per-image layouts and fences, and its depth
  attachment. `PresentationDesc::DepthStencil` decides whether its swapchain
  scope binds one; the image is made the first frame the scope records.
- **Scope.** `FeatureRegistration::Scope` (`graphics/RenderFeatureScope.h`) says
  where a feature records: `Global` (Offscreen only) or `Presentation(id)`. The
  default is the primary for a swapchain phase and `Global` for Offscreen. A
  swapchain-phase feature is set up against its presentation's swapchain and
  attachment formats (`PresentationTarget::SwapchainFeatureServices`).
- **Isolation.** Each target's availability (minimized, zero extent, needs
  rebuild) and each swapchain's acquire and present result are its own; one
  target going out of date never fails the frame for the others.
- **Destruction.** `Renderer::DestroyPresentation` fails closed while any
  feature records into the presentation. Otherwise it leaves every later frame
  at once and retires through the frame clock, one frame past the current so
  its last present has run too, with no device idle. The callback then
  destroys the window: the surface always dies before its native window.
  `OpenPresentationWindow` and `ClosePresentationWindow`
  (`graphics/vulkan/PresentationWindows.h`) pair the two.
- **Events.** Every window's events go through `PlatformEventRouter`;
  `PlatformEventContext::WindowId` names the window. The authored UI and the
  debug overlay take only primary-window events, and a secondary window's
  minimize or focus does not touch the frame loop.

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
