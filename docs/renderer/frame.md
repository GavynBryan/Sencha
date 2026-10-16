# The Frame

## Phase placement

The renderer occupies four of the `FramePhase` slots
(`engine/include/runtime/FrameDriver.h`). Registration for all of them lives in
`engine/src/app/EngineFramePhases.cpp`.

| Phase | Renderer work |
|---|---|
| `AwaitFrameSlot` (0) | `VulkanFrameService::AwaitFrameSlot`: wait this frame slot's fence, advance retirement and the deletion queue |
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
DrawFrameScheduled                                           [FramePhase 12, after simulation]
  VulkanFrameService::BeginFrame
    AwaitFrameSlot, if the frame phase did not already
    destroy retired presentations, then run their callbacks
    note which of the primary's presents reached the display   [vkWaitForPresentKHR, timeout 0]
    start the frame's acquire deadline                       [two refreshes of the primary's display]
    for each acquirable presentation:
      vkAcquireNextImageKHR(what is left of the deadline)    [signals its ImageAvailable]
      VK_TIMEOUT or VK_NOT_READY -> this presentation skips the frame
      the primary: presents outstanding, late present wait    [vkWaitForPresentKHR, timeout 0]
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
    mark each image presented in its SwapchainImageLedger
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
| `presentId` | monotonic, primary only | `VK_KHR_present_id` | nothing; `BeginFrame` polls it with a zero timeout to estimate latency |

`RenderFinished` is per image, not per frame slot. A per-slot signal semaphore
would be waited by a present for an image another slot is still using when the
swapchain has more images than frames in flight.

Per-image state, the `SwapchainImageLedger` included, lives on the
`PresentationTarget` and is dropped when its swapchain is rebuilt, so a recorded
fence never describes a dead chain.

## Pacing

Five concerns, each with one owner, none waiting on another's events:

| Concern | Owner | What bounds it |
|---|---|---|
| Simulation time | `RuntimeFrameLoop` (`FixedStepScheduler`, `FrameCadenceLock`) | wall time; ticks never depend on how frames present |
| CPU frame production | `FrameDriver` phases | the waits below |
| GPU execution | `VulkanFrameService::AwaitFrameSlot` | the slot fence: frames in flight |
| Presentation | `VulkanFrameService::AcquirePresentations` | images coming back from the display, up to the acquire deadline |
| Frame-rate limit | `FramePacer` (`r.target_fps`, 0 = off) | a sleep then spin at the end of the frame |

A frame waits in three places: the slot fence in `AwaitFrameSlot`, before input
is sampled; acquire in `BeginFrame`, after simulation, followed by the fence of
the frame that last rendered the acquired image; and the frame-rate limit at
its end. Nothing waits for present wait. In FIFO, acquire is where the
display paces the loop, and the input the frame sampled ages by that wait.

What the engine knows about the display falls into three kinds, and the docs,
capture and tests keep them apart.

### Guaranteed by the Vulkan API

With *F* frames in flight (`graphics.framesInFlight`, default 2, clamped to
1..4 by `graphics/FramesInFlight.h`):

- **Presents outstanding are at most `imageCount` minus the images the
  application holds.** A frame acquires in `BeginFrame` and presents in
  `EndFrame`, one image per presentation, and never holds one across frames:
  frames that exit, resize or minimize never reach the `Render` phase, and a
  frame that acquired nothing records nothing. So while a frame records, at most
  `imageCount - 1` of a chain's presents are outstanding.
- **The slot fence means the GPU finished frame *N - F*.** Frame *N* waits the
  fence of the frame that last used its slot, so the CPU is never more than *F*
  frames ahead of GPU completion. The fence covers commands, not the present
  queued after them.
- **MAILBOX has a single-entry display queue**: a newer present replaces the
  one waiting. **IMMEDIATE has none.** FIFO and FIFO_RELAXED show presents in
  order, but the API reports neither the queue's length nor which image the
  display still holds; only an acquire proves an image came back.

### Modelled

- **The display holds one image**, the one it is showing. On that assumption
  the swapchain has `max(F + 2, minImageCount)` images, capped by
  `maxImageCount` (`SwapchainImageCount`): room for the CPU to run up to *F*
  presents ahead with one image being recorded. A display or compositor that
  holds more shows up as acquire wait, which the capture measures exactly.
  `DisplayQueueTests` runs a FIFO display holding one, two or three images
  against the frame service's waits.

### Estimated

- **Latency.** `VK_KHR_present_id` and `VK_KHR_present_wait` say when the
  presentation engine reports a present displayed. `PresentHistory` keeps each
  primary present from queueing until then, which yields the present latency
  (queued to reported) and the frame latency (frame start, just before input is
  sampled, to reported). Both are estimates: present wait completes when the
  driver reports the present, which can be later than the display showed it,
  and `BeginFrame` polls it with a zero timeout, so a completion reads late by
  up to one frame. A late completion moves the estimate and nothing else.

The engine claims no fixed latency in refreshes and no exact queue length.

### Acquire deadline

Every acquire of one frame shares one deadline: two refresh periods of the
primary presentation's display (`kAcquireDeadlineRefreshes`), started when the
frame begins acquiring. Each `vkAcquireNextImageKHR` gets what is left of it, so
presentations whose display returns no image cost the frame at most the
deadline together, and one acquiring after them still gets an image the display
has already returned (a zero timeout acquires an available image). Two refreshes
is a policy, not a derived bound: long enough for a FIFO display that returns an
image each refresh to return one even when the frame started just after a
refresh, short enough that a display which stopped returning images (a hidden,
locked or stalled window) slows the loop to one frame per two refreshes instead
of stopping it.

`VK_TIMEOUT` or `VK_NOT_READY` means that presentation skips the frame. Neither
result signals the acquire semaphore, so its frame slot reuses it as is. Out of
date and fatal results are handled as before.

- **The primary skipped, others acquired.** Simulation and extraction already
  ran; the frame records and presents the others, features scoped to the
  primary do not record, and the frame reports `SkippedNotReady`.
- **Nothing acquired.** `BeginFrame` returns `NothingAcquired`: nothing records
  or submits, the frame slot and frame number are not consumed, and the frame
  reports `SkippedNotReady` when the primary timed out, `SwapchainOutOfDate`
  when it went out of date, and `SkippedMinimized` otherwise.

When the primary's display reports no refresh rate there is no deadline:
acquire waits as long as the presentation engine needs (`UINT64_MAX`). The
deadline is measured in refreshes, and with none to measure any number would be
a guess about a display nothing is known of. Vulkan allows the infinite wait
because the application holds no image of the chain when it acquires. The cost
is that such a display, if it stops returning images, holds the loop.

### Capture

| Field | Kind | Meaning |
|---|---|---|
| `frame_slot_wait_ms` | exact | the slot fence wait in `AwaitFrameSlot` |
| `acquire_wait_ms` | exact | time inside `vkAcquireNextImageKHR`, every presentation |
| `image_fence_wait_ms` | exact | waits for an acquired image's last frame |
| `presents_outstanding_max` | upper bound | the primary's images presented and not yet acquired again, counted when it acquires; at most `imageCount - 1`; -1 when the primary did not acquire |
| `present_wait_late_count` | exact | 1 when the primary acquired an image whose last present had not completed present wait yet: the completion arrived after the display let the image go. In MAILBOX and IMMEDIATE it also counts presents replaced before they were shown |
| `present_latency_estimate_ms` | estimate | newest present reported displayed, from its queueing; -1 if none |
| `frame_latency_estimate_ms` | estimate | the same present, from its frame's start; -1 if none |

**Frame time smoothness** belongs to the time domain. The loop's measured
deltas carry scheduler and compositor noise however frames are paced;
`FrameCadenceLock` removes it before simulation consumes the delta
(`time.cadence_lock`). Starving the swapchain of images to make acquire the
"vsync anchor", as the engine once did, removed no noise; it serialized the
pipeline.

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
captures to the interval between presented frames (median, p95, p99, share of
intervals on one refresh period, long-then-short pairs), throughput, frames not
presented, the most presents outstanding, late present-wait completions, the
median of each wait, and the two latency estimates, or with `--series` a long
session as a time series; `scripts/bench_frame_pacing.sh` runs a matrix of
present modes, window sizes, limits and adapters against one build. A capture
armed for a bounded run (`app.exit_after_frames`) keeps every frame.
`app.frame_delay_ms` and `app.frame_delay_period` hold chosen frames after their
input, to reproduce hitches and slow frames.
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
| `SkippedNotReady` | the primary returned no image by the acquire deadline | nothing; the frame simulated, and the next one acquires again |

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
- `VulkanFrameTiming`: frame-slot, acquire, image-fence, submit and present
  seconds, the present and frame latency estimates of the newest present seen
  displayed, the primary's presents outstanding and late present wait, image
  index and swapchain generation.
- `SwapchainState`: extent, format, color space, present mode, image counts,
  generation, and the cumulative recreate count.
- The `RenderFrameResult`.
- The last collected GPU scope spans and this frame's CPU scope milliseconds,
  when instrumentation is active.

Then `Engine::PushRenderStatsFrame` appends the frame's `RenderStats` to the
history ring and, in Capture mode, to the capture ring.
