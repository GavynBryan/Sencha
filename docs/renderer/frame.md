# The Frame

## Phase placement

The renderer occupies four of the `FramePhase` slots
(`engine/include/runtime/FrameDriver.h`). Registration for all of them lives in
`engine/src/app/EngineFramePhases.cpp`.

| Phase | Renderer work |
|---|---|
| `AwaitFrameSlot` (0) | `VulkanFrameService::AwaitFrameSlot`: wait this frame slot's fence, advance retirement and the deletion queue, then hold to the queued-present bound |
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
  display-paced primary: wait until at most F presents are queued [vkWaitForPresentKHR, bounded]
  note which of the primary's presents reached the display   [vkWaitForPresentKHR, timeout 0]
DrawFrameScheduled
  VulkanFrameService::BeginFrame
    AwaitFrameSlot, if the frame phase did not already
    destroy retired presentations, then run their callbacks
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
| `presentId` | monotonic, primary only | `VK_KHR_present_id` | `AwaitFrameSlot`: waited for present *N - 1 - F* in a display-paced mode, then every pending one polled with a zero timeout |

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
| GPU execution | `VulkanFrameService::AwaitFrameSlot` | the slot fence: frames in flight |
| Presentation | `VulkanFrameService::AwaitFrameSlot` (display-paced primary) or acquire | presents queued for the display |
| Frame-rate limit | `FramePacer` (`r.target_fps`, 0 = off) | a sleep then spin at the end of the frame |

### Four quantities

With *F* frames in flight (`graphics.framesInFlight`, default 2, clamped to
1..4 by `graphics/FramesInFlight.h`):

1. **GPU frames in flight, at most *F*.** One fence per slot. Frame *N* waits
   the fence of frame *N - F* in `AwaitFrameSlot`, before input, so the CPU is
   never more than *F* frames ahead of GPU completion and CPU and GPU overlap
   by up to *F* frames.
2. **Presents queued but not yet displayed, at most *F* when a frame begins**
   (`MaxQueuedPresents`). This is a different count from (1): a present whose
   GPU work finished long ago still waits its turn for a refresh. In a
   display-paced mode (FIFO, FIFO_RELAXED) with present wait, the primary's
   frame *N* waits in `AwaitFrameSlot`, before input, until present
   *N - 1 - F* is displayed. That is the only display wait in the frame.
3. **Images the application owns while recording: one.** A frame acquires in
   `BeginFrame` and presents in `EndFrame`; frames do not hold images across
   each other.
4. **Swapchain images: `max(F + 2, minImageCount)`, capped by
   `maxImageCount`** (`SwapchainImageCount`). That is one on the display, *F*
   queued behind it and one being recorded. Acquire therefore never waits
   while (2) holds, and without the hold it bounds the queue to the same *F*.

### Why *F* presents

A frame's CPU and GPU work may span up to *F* refreshes: at one frame per
refresh, frame *N + F* waits for frame *N*'s fence, so (1) already allows it
that long. The presentation engine then needs up to one more refresh to latch
the finished image, more on a composited desktop than for a surface scanned out
directly. A frame started when present *N - 1 - F* is displayed has *F + 1*
refreshes before its own refresh comes up, which is exactly that budget. Holding
one present shallower takes the presentation refresh away and starves the
display; holding one deeper adds a refresh of latency and buys nothing.
`DisplayQueueTests` checks both directions against a FIFO model of the
pipeline: one present shallower, the depth the engine used before, feeds a
path that takes most of a refresh only two refreshes in three. The bound is a count
of presents, not of milliseconds, so it holds at any refresh rate and is not
tuned to a machine (measurements: `docs/plans/evidence/frame-pacing/README.md`).

### Latency bound

When the display paces the loop, frame *N* begins (and samples input) right
after present *N - 1 - F* is displayed and is itself displayed *F + 1*
refreshes later: input to display is *F + 1* refreshes, 20.8 ms at 144 Hz and
50 ms at 60 Hz with the default *F = 2*. `graphics.framesInFlight 1` gives two
refreshes at the cost of serializing CPU and GPU work. When a frame-rate limit
below the refresh rate paces the loop the queue stays near empty and latency is
the frame's own production and presentation path.

If the display cannot keep up, because the presentation path takes longer than
its refresh or the compositor stalls, the hold keeps the queue at *F* and the
frame rate falls instead: frames are not queued to buy throughput. The hold
waits at most *F + 2* refreshes, so a display that never reports a present
displayed (a hidden or locked window) slows the loop rather than stopping it,
provided the presentation engine keeps returning images: acquire has no
timeout.

### Where the hold does not apply

Without `VK_KHR_present_wait`, without a known refresh rate, for secondary
presentations, and in MAILBOX and IMMEDIATE, nothing holds a frame for the
display. In FIFO the image count then bounds the queue to the same *F*, but the
wait moves into acquire, after input, and ages the input by its length.
MAILBOX and IMMEDIATE do not queue: GPU backpressure and the optional
frame-rate limit bound them. The capture reports `present_wait_ms`,
`acquire_ms`, `queued_presents_count` (presents not yet seen displayed when the
frame began, after the hold) and `present_latency_ms` / `frame_latency_ms`.

**Observing the display.** `VK_KHR_present_id` and `VK_KHR_present_wait` say
when a present reaches the display. `PresentHistory` keeps each primary
present from queueing until it is seen displayed, which yields
`present_latency_ms` (queued to displayed) and `frame_latency_ms` (frame start,
just before input is sampled, to displayed). Both are estimates of display
time: present wait completes when the presentation engine reports the present,
not when light leaves the panel. A present seen by the zero-timeout poll reads
late by up to one frame interval; one the frame waited for is exact.

**Frame time smoothness** belongs to the time domain. The loop's measured
deltas carry scheduler and compositor noise however frames are paced;
`FrameCadenceLock` removes it before simulation consumes the delta
(`time.cadence_lock`). Starving the swapchain of images to make acquire the
"vsync anchor", as the engine once did, removed no noise; it serialized the
pipeline. A hold-bound loop is released by display events one refresh apart,
which is the steadiest cadence the loop can measure.

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
intervals on one refresh period, long-then-short pairs), throughput, the
deepest queue, the median of each wait, and the two latencies, or with
`--series` a long session as a time series; `scripts/bench_frame_pacing.sh`
runs a matrix of present modes, window sizes, limits and adapters against one
build. A capture armed for a bounded run (`app.exit_after_frames`) keeps every
frame. `app.frame_delay_ms` and `app.frame_delay_period` hold chosen frames
after their input, to reproduce hitches and slow frames.
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
  seen displayed, the presents queued at frame start, image index and swapchain generation.
- `SwapchainState`: extent, format, color space, present mode, image counts,
  generation, and the cumulative recreate count.
- The `RenderFrameResult`.
- The last collected GPU scope spans and this frame's CPU scope milliseconds,
  when instrumentation is active.

Then `Engine::PushRenderStatsFrame` appends the frame's `RenderStats` to the
history ring and, in Capture mode, to the capture ring.
