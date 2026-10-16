# The Frame

## Phase placement

The renderer occupies four of the `FramePhase` slots
(`engine/include/runtime/FrameDriver.h`). Registration for all of them lives in
`engine/src/app/EngineFramePhases.cpp`.

| Phase | Renderer work |
|---|---|
| `AwaitFrameSlot` (0) | `VulkanFrameService::AwaitFrameSlot`: wait this frame slot's fence, advance retirement and the deletion queue, then hold to the display queue limit |
| `RebuildGraphics` (3) | `VulkanFrameService::RebuildPresentation` for the primary when the frame loop says so, then `RebuildStaleSecondaries` |
| `ExtractRender` (11) | latch the profile mode, propagate visible transforms, run every registered extract system (`DefaultRenderPipeline::ExtractRender`) |
| `Render` (12) | `Renderer::DrawFrameScheduled`, then push the timing sample and the stats frame |

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
AwaitFrameSlot                                               [FramePhase 0, before input]
  wait on this slot's in-flight fence (if it was submitted)
  VulkanDeletionQueueService::AdvanceFrame
  display-paced primary: wait until present N - depth is displayed [vkWaitForPresentKHR, bounded]
DrawFrameScheduled
  VulkanFrameService::BeginFrame
    AwaitFrameSlot, if the frame phase did not already
    destroy retired presentations, then run their callbacks
    note which of the primary's presents reached the display [vkWaitForPresentKHR, timeout 0]
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
| `InFlightFence` | one per frame in flight | that frame's submit | next `AwaitFrameSlot` for the same slot, and by any frame acquiring an image it last rendered |
| `presentId` | monotonic, primary only | `VK_KHR_present_id` | `AwaitFrameSlot`, for present *N - depth* in a display-paced mode; polled with a zero timeout at each `BeginFrame` |

`RenderFinished` is per image, not per frame slot. A per-slot signal semaphore
would be waited by a present for an image another slot is still using when the
swapchain has more images than frames in flight.

Per-image state lives on the `PresentationTarget` and is dropped when its
swapchain is rebuilt, so a recorded fence never describes a dead chain.

## Pacing

Five concerns, each with one owner, none waiting on another's events:

| Concern | Owner | What bounds it |
|---|---|---|
| Simulation time | `RuntimeFrameLoop` (`FixedStepScheduler`, `FrameCadenceLock`) | wall time; ticks never depend on how frames present |
| CPU frame production | `FrameDriver` phases | the waits below |
| GPU execution | `VulkanFrameService::AwaitFrameSlot` | frames in flight: the CPU is at most `graphics.framesInFlight` frames ahead of GPU completion |
| Presentation | the swapchain (present mode, image count) and `DisplayQueueLimit` | in display-paced modes, how many presents may wait for the display when a frame begins; acquire, only once every image the application may own is queued |
| Frame-rate limit | `FramePacer` (`r.target_fps`, 0 = off) | a sleep then spin at the end of the frame |

**Observing the display.** `VK_KHR_present_id` and `VK_KHR_present_wait` say
when a present reaches the display. `PresentHistory` keeps each primary
present from queueing until it is seen displayed, which yields
`present_latency_ms` (queued to displayed) and `frame_latency_ms` (frame start,
just before input is sampled, to displayed). A present seen by the zero-timeout
poll in `BeginFrame` reads late by up to one frame interval; one the frame
waited for is exact.

**The display queue limit.** In a display-paced mode a frame that begins while
more presents wait for the display than the display needs only adds latency:
it will wait in the queue. `AwaitFrameSlot` therefore holds a frame, before it
samples input, until present *N - depth* has been displayed. The right depth is
the presentation path's latency in refreshes plus the frame's production time,
and that latency belongs to the platform: about one refresh for a surface
scanned out directly, two on a composited desktop, three to four on a laptop
whose discrete GPU presents through the integrated one. A fixed depth is wrong
somewhere. The engine used one, 2, before every frame and in every present
mode: on that laptop it held FIFO at two thirds of 144 Hz, and IMMEDIATE and
MAILBOX at the same rate.

`DisplayQueueLimit` learns the depth. The latency of a present held to the
limit measures the limit, not the path, so it cannot be read off the latency;
it can be read off starvation. When the display shows a frame for two refreshes
although the next frame was released by the limit and produced within one, the
limit released it too late. Two such refreshes within a second are a pattern,
not a hitch, and deepen the limit by one. Nothing makes it shallower: probing
downward would starve the display on purpose. It starts at the frames in
flight, is capped by what `PresentHistory` can track, and restarts when the
primary swapchain is rebuilt, since a new chain may take another path
(fullscreen scanout, a composited window). A frame produced in more than a
refresh is the application's limit and never deepens it. The wait is bounded
by the depth plus one refresh, so a display that stops taking frames, hidden or
locked, cannot hold the loop. Without present wait, or without a known refresh
rate, there is no limit and acquire alone applies. The capture reports the
depth each frame (`display_queue_depth_count`) and the wait
(`present_wait_ms`).

**Display-paced and unpaced modes.** FIFO and FIFO_RELAXED are display-paced:
the presentation engine consumes one image per refresh, so a present queued
early waits. MAILBOX and IMMEDIATE replace or tear instead; nothing in them
waits for the display, and GPU backpressure and the optional frame-rate limit
bound them.

**Image count.** `SwapchainImageCount` (`graphics/FramesInFlight.h`) asks for
`minImageCount + framesInFlight - 1`. The presentation engine may hold
`minImageCount` images at once, so this lets the application own one image per
frame in flight. Asking for `minImageCount` exactly, as the engine used to, lets
the application own one image: the next frame cannot begin recording until the
display hands back the image the last one used, so recording, GPU execution and
display run in series and any frame longer than the time the display leaves
drops to half refresh.

**Latency bound.** Frames ahead of the display are bounded by the display
queue limit, the frames in flight and the swapchain's images. Both waits that
throttle a frame in steady state, the GPU slot and the display queue, come
before it samples input (`FramePhase::AwaitFrameSlot` is phase 0), so neither
ages the input. Acquire runs inside `Render`, after input; it blocks only when
the limit has not bound, by at most one refresh interval.

**Frame time smoothness** belongs to the time domain. The loop's measured
deltas carry scheduler and compositor noise however frames are paced;
`FrameCadenceLock` removes it before simulation consumes the delta
(`time.cadence_lock`). Starving the swapchain of images to make acquire the
"vsync anchor" removed no noise; it serialized the pipeline. A limit-bound
loop is released by display events one refresh apart, which is the steadiest
cadence the loop can measure.

**Frame-rate limit with vsync.** `r.target_fps` limits frame starts. In FIFO a
limit at or above the refresh rate is moot. A limit below it produces frames at
the limit, which a fixed-refresh display shows for one or two refreshes each, so
a 120 limit on a 144 Hz panel presents an uneven 5-in-6 cadence. That is the
arithmetic of a fixed refresh rate, not a scheduling fault; choose a divisor of
the refresh rate, or no limit, when vsync is on.

Present mode selection: FIFO by default, overridable per process with the
`SENCHA_PRESENT_MODE` environment variable (`IMMEDIATE`, `MAILBOX`, `FIFO`,
`FIFO_RELAXED`).

Benchmarks: `scripts/frame_pacing_stats.py` reduces `render.capture.output`
captures to the frame interval distribution (median, p95, p99, share of
intervals on one refresh period), throughput, the median of each wait, and the
two latencies; `scripts/bench_frame_pacing.sh` runs a matrix of present modes,
window sizes, limits and adapters against one build.
`docs/plans/evidence/frame-pacing/README.md` records the measurements behind
this section.

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
- `VulkanFrameTiming`: frame-fence, present-wait, acquire, image-fence, submit
  and present seconds, the present and frame latency of the newest present
  seen displayed, the display queue depth, image index and swapchain generation.
- `SwapchainState`: extent, format, color space, present mode, image counts,
  generation, and the cumulative recreate count.
- The `RenderFrameResult`.
- The last collected GPU scope spans and this frame's CPU scope milliseconds,
  when instrumentation is active.

Then `Engine::PushRenderStatsFrame` appends the frame's `RenderStats` to the
history ring and, in Capture mode, to the capture ring.
