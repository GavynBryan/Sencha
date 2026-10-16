# Frame pacing: measurement evidence

Measurements behind `docs/renderer/frame.md` (Pacing): why the engine held a
near-empty map at a fraction of the display's refresh rate, what fixed it, and
why the fix needs no adaptive display queue.

## Machine and method

- Laptop: RTX 4060 Laptop GPU plus Intel Raptor Lake-P graphics, KWin 6.6.5
  Wayland, internal panel 1920x1080 at 144 Hz, no VRR. The panel hangs off the
  Intel GPU, so every NVIDIA present crosses GPUs (PRIME). Device index `0` is
  Intel; `auto` picks NVIDIA. Mesa's Wayland surfaces report `minImageCount`
  3, NVIDIA's 2.
- Driver health: the NVIDIA driver on this machine degrades after long uptime.
  Every number below was taken within about two hours of a reboot, with
  `vkcube --gpu_number 1 --present_mode 2 --c 1440 --width 1920 --height 1080`
  at 10.2 to 10.4 s (141 fps) before each NVIDIA block. One check right after
  a long idle took 31 s and the next one 10.3 s; check twice before blaming
  the engine.
- Scene: `templates/fps`, `levels/room_2` (a near-empty FPS-template room).
- Builds, all Release (`profile` preset): `main` is origin/main 8f13cb5e;
  `HEAD` is the frame-pacing branch at c2459c94 (learned display queue limit,
  one extra image on held chains); `simplified` is the fixed bound described
  in `frame.md`.
- Runner: `scripts/bench_frame_pacing.sh`, 900 frames per run, two runs per
  cell pooled, reduced by `scripts/frame_pacing_stats.py` after dropping 120
  warm-up frames. Windowed sizes are requested through `SENCHA_WINDOW_SIZE`;
  KWin trims 1920x1080 windowed to 1908x1042. No compiler ran during any run.
- `main` crashes at shutdown in some runs (exit 139, after its capture is
  written); that is the separate shutdown crash, not pacing.

Columns: `fps` is frame starts per second; `med`/`p99` the frame interval in
ms; `@1T%` the share of intervals within a quarter of one refresh (6.94 ms);
`LS%` the share of consecutive interval pairs that are long (at least 1.5
refreshes) then short (at most half of one), the microstutter signature;
`q_max` the most presents seen queued when a frame began; `acquire` and
`pres_wait` per-frame medians in ms; `frame_lat` the median and `lat_p99` the
99th percentile of frame start (just before input) to displayed, in ms.
Latencies are estimates from present-wait completion, not a display
measurement; `main` reports none.

Fixed ticks per second are not tabulated: a run whose window loses focus
pauses the game, which stops fixed ticks, and the owner was using the machine
during the matrix. Runs that kept focus read 59.7 to 60.5 ticks per second at
the 60 Hz tick in all three builds; `FrameDriver.TicksFollowWallTimeHoweverFramesWaitForTheDisplay`
covers the invariant.

## Root causes

1. **The application could own one swapchain image.** `main` asked for
   `minImageCount` images, so a frame could not record until the display
   handed back the image the previous frame used. On NVIDIA fullscreen the
   frame waits 6.9 to 13.7 ms in acquire, after input, and runs at 79 to 108
   fps.
2. **The display wait sat after input and one present too shallow.** `main`
   waited, inside `BeginFrame`, for the present two frames back. With the
   presentation path taking most of a refresh that starves the display; the
   FIFO model in `DisplayQueueTests` gives exactly two refreshes in three at
   that depth, and windowed 1080p on both GPUs misses 1 to 2 percent of
   refreshes. Intel windowed 1080p with a 120 or 144 limit locks into about
   32 ms frames (38 to 47 fps) in every `main` run.
3. **Every frame waited for the display in every present mode.** `main` held
   IMMEDIATE to the same display wait, 107 to 138 fps.

## The chosen bound

A display-paced frame begins only when at most `F` presents (one per frame in
flight, 2 by default) wait for the display, and the swapchain has
`max(F + 2, minImageCount)` images. `frame.md` derives both. Measured, the
bound sits at `q_max` 2 and `frame_lat` 20.8 ms (three refreshes) in every
held cell on both GPUs, and acquire never waits (median 0.01 to 0.02 ms) on
either GPU, fullscreen included.

## Three-build comparison

| GPU | mode | size | limit | build | img | fps | med | p99 | @1T% | LS% | q_max | acquire | pres_wait | frame_lat | lat_p99 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| NVIDIA | FIFO | fullscreen | 0 | origin/main | - | 108.2 | 6.98 | 14.04 | 67 | 0.0 | - | 6.85 | - | - | - |
| NVIDIA | FIFO | fullscreen | 0 | HEAD c2459c94 | 4 | 144.0 | 6.95 | 7.21 | 100 | 0.0 | - | 0.01 | 6.85 | 20.4 | 21.0 |
| NVIDIA | FIFO | fullscreen | 0 | simplified | 4 | 144.0 | 6.94 | 7.24 | 100 | 0.0 | 2 | 0.01 | 6.84 | 20.8 | 21.0 |
| NVIDIA | FIFO | fullscreen | 120 | origin/main | - | 78.9 | 13.86 | 18.36 | 23 | 0.0 | - | 13.67 | - | - | - |
| NVIDIA | FIFO | fullscreen | 120 | HEAD c2459c94 | 4 | 120.0 | 8.33 | 8.33 | 100 | 0.0 | - | 0.01 | 0.00 | 16.7 | 16.8 |
| NVIDIA | FIFO | fullscreen | 120 | simplified | 4 | 120.0 | 8.33 | 8.34 | 100 | 0.0 | 2 | 0.01 | 0.00 | 16.7 | 16.7 |
| Intel | FIFO | 1920x1080 | 0 | origin/main | - | 141.5 | 6.95 | 13.75 | 98 | 0.0 | - | 0.02 | - | - | - |
| Intel | FIFO | 1920x1080 | 0 | HEAD c2459c94 | 5 | 144.0 | 6.94 | 7.17 | 100 | 0.0 | - | 0.02 | 6.71 | 20.8 | 21.0 |
| Intel | FIFO | 1920x1080 | 0 | simplified | 4 | 143.7 | 6.94 | 7.42 | 100 | 0.1 | 2 | 0.02 | 6.74 | 20.8 | 21.4 |

`HEAD` and `simplified` match: 144 fps, every interval on one refresh, no
long-then-short pairs, the queue at 2 and three refreshes of latency. HEAD's
learned depth sat at 3 in every run, which is the fixed bound. `main` stays
below refresh in all three cells.

## Simplified build, other modes and limits

| GPU | mode | size | limit | build | img | fps | med | p99 | @1T% | LS% | q_max | acquire | pres_wait | frame_lat | lat_p99 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| NVIDIA | FIFO | 1280x720 | 60 | simplified | 4 | 60.0 | 16.67 | 16.68 | 0 | 0.0 | 1 | 0.02 | 0.00 | 16.7 | 16.7 |
| NVIDIA | FIFO | 1280x720 | 144 | simplified | 4 | 144.0 | 6.94 | 6.95 | 100 | 0.0 | 2 | 0.02 | 5.53 | 20.8 | 21.3 |
| NVIDIA | IMMEDIATE | 1280x720 | 0 | simplified | 4 | 1871.8 | 0.46 | 2.66 | 0 | 0.0 | 8 | 0.01 | 0.00 | 3.2 | 5.2 |
| Intel | IMMEDIATE | 1280x720 | 0 | simplified | 4 | 5324.0 | 0.17 | 0.69 | 0 | 0.0 | 5 | 0.01 | 0.00 | 0.7 | 1.3 |

IMMEDIATE is not held: thousands of frames per second, a few ms from start to
display; its `q_max` counts presents not yet reported, which nothing waits on. Limits hold their rate to within 0.01 ms at p99.

## The extra image (c2459c94)

c2459c94 gave a chain one image beyond `minImageCount + F - 1` wherever the
display queue limit held it, on degraded-driver evidence (88 to 102 to 105
fps on NVIDIA fullscreen). On a healthy driver the derived count makes it
unnecessary: NVIDIA gets 4 images under both (`max(F + 2, 2)` and `2 + 1 + 1`),
and Intel 4 instead of HEAD's 5. With 4 images Intel matches HEAD's 5 in
every cell: windowed 1080p FIFO 143.7 against 144.0 fps with p99 7.42 against
7.17 ms, fullscreen 144.0 against 143.0, and acquire at 0.02 ms in both. The
first fix's 3 images on NVIDIA (adbab6c8, measured after the reboot) did make
acquire the binding wait fullscreen, 6.6 ms after input; the derived count
removes that, so the image the commit added on NVIDIA survives, for a stated
reason, and the policy behind it does not.

## The first fix after the reboot (Debug)

Measured by the coordinating session after the reboot, `dev` preset, FIFO,
two runs per cell: `main` against adbab6c8, the first fix (pre-input hold,
`minImageCount + F - 1` images, learned depth). The learned depth sat at 3,
the fixed bound, in every run. On NVIDIA with 3 images, fullscreen made
acquire the binding wait (6.6 ms after input); the simplified count of 4
removes that.

| GPU | size | limit | build | img | fps | p99 | @1T% | acquire | pres_wait | frame_lat |
|---|---|---|---|---|---|---|---|---|---|---|
| Intel | 1920x1080 | 0 | adbab6c8 | 4 | 144.0 | 7.57 | 100 | 0.01 | 6.55 | 20.8 |
| Intel | 1920x1080 | 0 | main | - | 137.9 | 13.87 | 96 | 0.01 | - | - |
| Intel | fullscreen | 0 | adbab6c8 | 4 | 144.0 | 7.46 | 100 | 0.01 | 6.56 | 20.8 |
| Intel | fullscreen | 0 | main | - | 144.0 | 7.14 | 100 | 0.01 | - | - |
| NVIDIA | 1280x720 | 0 | adbab6c8 | 3 | 144.0 | 7.44 | 100 | 0.01 | 6.55 | 20.8 |
| NVIDIA | 1280x720 | 0 | main | - | 143.9 | 7.22 | 100 | 0.01 | - | - |
| NVIDIA | 1280x720 | 120 | adbab6c8 | 3 | 120.0 | 8.34 | 100 | 0.01 | 0.00 | 17.0 |
| NVIDIA | 1280x720 | 120 | main | - | 120.0 | 8.34 | 100 | 0.01 | - | - |
| NVIDIA | 1920x1080 | 0 | adbab6c8 | 3 | 144.0 | 7.52 | 100 | 0.01 | 6.56 | 20.8 |
| NVIDIA | 1920x1080 | 0 | main | - | 138.5 | 13.90 | 96 | 0.01 | - | - |
| NVIDIA | 1920x1080 | 120 | adbab6c8 | 3 | 120.0 | 8.34 | 100 | 0.01 | 0.00 | 17.0 |
| NVIDIA | 1920x1080 | 120 | main | - | 119.9 | 8.34 | 100 | 0.01 | - | - |
| NVIDIA | fullscreen | 0 | adbab6c8 | 3 | 144.0 | 7.14 | 100 | 6.55 | 0.00 | 21.1 |
| NVIDIA | fullscreen | 0 | main | - | 94.2 | 14.10 | 47 | 12.95 | - | - |
| NVIDIA | fullscreen | 120 | adbab6c8 | 3 | 120.0 | 8.34 | 100 | 0.01 | 0.00 | 17.0 |
| NVIDIA | fullscreen | 120 | main | - | 82.8 | 14.15 | 32 | 13.28 | - | - |

Measurements in this file before 2026-10-16 (commit 950388a4) were taken on
the degraded driver: present latencies of 20 to 28 ms, queues growing to 4 to
7 presents and a `vkcube` ceiling of 82 fps on NVIDIA. They motivated the
learned depth and the extra image, and are withdrawn.

## Sustained sessions, stall injection and transitions

Not yet run; see "Frame-pacing validation on a display" in `docs/deferred.md`.

## Full matrix

Two runs per cell, pooled.

| GPU | mode | size | limit | fps main / HEAD / simplified | p99 ms | @1T% | frame_lat ms HEAD / simplified | img main→HEAD→simplified |
|---|---|---|---|---|---|---|---|---|
| NVIDIA | FIFO | 1280x720 | 0 | 142.7 / 143.9 / 144.0 | 8.11 / 7.35 / 7.36 | 99 / 99 / 100 | 20.8 / 20.8 | 2→4→4 |
| NVIDIA | FIFO | 1280x720 | 60 | 60.0 / 60.0 / 60.0 | 16.67 / 16.67 / 16.68 | 0 / 0 / 0 | 16.8 / 16.7 | 2→4→4 |
| NVIDIA | FIFO | 1280x720 | 120 | 120.0 / 119.1 / 120.0 | 8.33 / 8.34 / 8.34 | 100 / 100 / 100 | 16.8 / 16.7 | 2→4→4 |
| NVIDIA | FIFO | 1280x720 | 144 | 143.8 / 143.1 / 144.0 | 6.95 / 6.95 / 6.95 | 100 / 100 / 100 | 20.8 / 20.8 | 2→4→4 |
| NVIDIA | FIFO | 1920x1080 | 0 | 142.3 / 144.0 / 144.0 | 13.43 / 7.21 / 7.20 | 99 / 100 / 100 | 20.8 / 20.8 | 2→4→4 |
| NVIDIA | FIFO | 1920x1080 | 60 | 60.0 / 60.0 / 60.0 | 16.67 / 16.67 / 16.67 | 0 / 0 / 0 | 16.7 / 16.7 | 2→4→4 |
| NVIDIA | FIFO | 1920x1080 | 120 | 120.0 / 120.0 / 120.0 | 8.33 / 8.34 / 8.33 | 100 / 100 / 100 | 16.7 / 16.7 | 2→4→4 |
| NVIDIA | FIFO | 1920x1080 | 144 | 143.3 / 144.0 / 143.9 | 7.03 / 6.95 / 6.95 | 100 / 100 / 100 | 20.8 / 20.8 | 2→4→4 |
| NVIDIA | FIFO | fullscreen | 0 | 108.2 / 144.0 / 144.0 | 14.04 / 7.21 / 7.24 | 67 / 100 / 100 | 20.4 / 20.8 | 2→4→4 |
| NVIDIA | FIFO | fullscreen | 60 | 60.0 / 60.0 / 60.0 | 16.67 / 16.67 / 16.67 | 0 / 0 / 0 | 16.7 / 16.7 | 2→4→4 |
| NVIDIA | FIFO | fullscreen | 120 | 78.9 / 120.0 / 120.0 | 18.36 / 8.33 / 8.34 | 23 / 100 / 100 | 16.7 / 16.7 | 2→4→4 |
| NVIDIA | FIFO | fullscreen | 144 | 94.7 / 144.0 / 144.0 | 14.14 / 6.95 / 6.95 | 49 / 100 / 100 | 20.8 / 20.8 | 2→4→4 |
| NVIDIA | IMMEDIATE | 1280x720 | 0 | 137.5 / 1980.4 / 1871.8 | 13.95 / 2.01 / 2.66 | 95 / 0 / 0 | 2.7 / 3.2 | 2→4→4 |
| NVIDIA | IMMEDIATE | 1280x720 | 60 | 60.0 / 60.0 / 60.0 | 16.67 / 16.67 / 16.67 | 0 / 0 / 0 | 16.8 / 16.7 | 2→4→4 |
| NVIDIA | IMMEDIATE | 1280x720 | 120 | 118.0 / 119.4 / 120.0 | 12.49 / 8.34 / 8.33 | 98 / 100 / 100 | 16.7 / 16.7 | 2→4→4 |
| NVIDIA | IMMEDIATE | 1280x720 | 144 | 123.6 / 144.0 / 144.0 | 14.08 / 6.95 / 6.95 | 85 / 100 / 100 | 20.9 / 20.8 | 2→4→4 |
| NVIDIA | IMMEDIATE | 1920x1080 | 0 | 126.2 / 1155.5 / 1226.9 | 14.06 / 1.97 / 1.17 | 85 / 0 / 0 | 4.8 / 4.8 | 2→4→4 |
| NVIDIA | IMMEDIATE | 1920x1080 | 60 | 60.0 / 60.0 / 60.0 | 16.67 / 16.67 / 16.67 | 0 / 0 / 0 | 16.8 / 16.7 | 2→4→4 |
| NVIDIA | IMMEDIATE | 1920x1080 | 120 | 111.9 / 119.7 / 119.6 | 12.70 / 8.34 / 8.34 | 87 / 100 / 100 | 16.8 / 16.7 | 2→4→4 |
| NVIDIA | IMMEDIATE | 1920x1080 | 144 | 143.0 / 144.0 / 143.3 | 7.20 / 6.95 / 6.95 | 99 / 100 / 100 | 20.9 / 20.8 | 2→4→4 |
| NVIDIA | IMMEDIATE | fullscreen | 0 | 107.1 / 938.9 / 961.2 | 14.06 / 5.94 / 3.97 | 66 / 1 / 0 | 5.0 / 4.9 | 2→4→4 |
| NVIDIA | IMMEDIATE | fullscreen | 60 | 60.0 / 60.0 / 60.0 | 16.67 / 16.67 / 16.67 | 0 / 0 / 0 | 16.8 / 16.7 | 2→4→4 |
| NVIDIA | IMMEDIATE | fullscreen | 120 | 118.4 / 120.0 / 120.0 | 12.47 / 8.34 / 8.34 | 98 / 100 / 100 | 16.7 / 16.7 | 2→4→4 |
| NVIDIA | IMMEDIATE | fullscreen | 144 | 119.9 / 144.0 / 144.0 | 14.02 / 6.95 / 6.95 | 80 / 100 / 100 | 20.9 / 13.9 | 2→4→4 |
| Intel | FIFO | 1280x720 | 0 | 143.6 / 144.0 / 144.0 | 7.29 / 7.30 / 7.33 | 100 / 100 / 100 | 20.5 / 20.8 | 3→5→4 |
| Intel | FIFO | 1280x720 | 60 | 60.0 / 60.0 / 60.0 | 16.67 / 16.67 / 16.67 | 0 / 0 / 0 | 16.7 / 16.7 | 3→5→4 |
| Intel | FIFO | 1280x720 | 120 | 120.0 / 120.0 / 120.0 | 8.34 / 8.34 / 8.33 | 100 / 100 / 100 | 16.7 / 16.7 | 3→5→4 |
| Intel | FIFO | 1280x720 | 144 | 144.0 / 143.6 / 144.0 | 6.95 / 6.99 / 6.95 | 100 / 100 / 100 | 20.9 / 20.8 | 3→5→4 |
| Intel | FIFO | 1920x1080 | 0 | 141.5 / 144.0 / 143.7 | 13.75 / 7.17 / 7.42 | 98 / 100 / 100 | 20.8 / 20.8 | 3→5→4 |
| Intel | FIFO | 1920x1080 | 60 | 45.0 / 60.0 / 60.0 | 35.78 / 16.67 / 16.67 | 0 / 0 / 0 | 16.8 / 16.7 | 3→5→4 |
| Intel | FIFO | 1920x1080 | 120 | 42.3 / 119.0 / 120.0 | 35.94 / 8.33 / 8.34 | 29 / 100 / 100 | 16.8 / 16.7 | 3→5→4 |
| Intel | FIFO | 1920x1080 | 144 | 38.2 / 141.8 / 144.0 | 35.94 / 7.05 / 6.95 | 21 / 99 / 100 | 20.8 / 20.8 | 3→5→4 |
| Intel | FIFO | fullscreen | 0 | 114.2 / 143.0 / 144.0 | 14.24 / 7.32 / 7.44 | 76 / 99 / 100 | 13.9 / 20.8 | 3→5→4 |
| Intel | FIFO | fullscreen | 60 | 60.0 / 60.0 / 60.0 | 16.67 / 16.67 / 16.67 | 0 / 0 / 0 | 16.8 / 16.7 | 3→5→4 |
| Intel | FIFO | fullscreen | 120 | 113.9 / 119.4 / 120.0 | 12.74 / 8.35 / 8.34 | 91 / 100 / 100 | 16.7 / 16.7 | 3→5→4 |
| Intel | FIFO | fullscreen | 144 | 134.4 / 142.8 / 143.2 | 13.94 / 7.01 / 6.95 | 94 / 99 / 100 | 13.9 / 20.8 | 3→5→4 |
| Intel | IMMEDIATE | 1280x720 | 0 | 137.8 / 2310.9 / 5324.0 | 13.94 / 1.00 / 0.69 | 96 / 0 / 0 | 1.5 / 0.7 | 3→5→4 |
| Intel | IMMEDIATE | 1280x720 | 60 | 60.0 / 60.0 / 60.0 | 16.67 / 16.67 / 16.67 | 0 / 0 / 0 | 16.8 / 16.7 | 3→5→4 |
| Intel | IMMEDIATE | 1280x720 | 120 | 114.9 / 119.9 / 120.0 | 12.63 / 8.34 / 8.34 | 92 / 100 / 100 | 16.7 / 16.7 | 3→5→4 |
| Intel | IMMEDIATE | 1280x720 | 144 | 143.8 / 144.0 / 144.0 | 6.95 / 6.95 / 6.95 | 100 / 100 / 100 | 14.0 / 13.9 | 3→5→4 |
| Intel | IMMEDIATE | 1920x1080 | 0 | 133.2 / 2090.4 / 2962.4 | 14.05 / 1.50 / 1.34 | 92 / 0 / 0 | 1.4 / 1.2 | 3→5→4 |
| Intel | IMMEDIATE | 1920x1080 | 60 | 60.0 / 60.0 / 60.0 | 16.67 / 16.67 / 16.67 | 0 / 0 / 0 | 16.8 / 16.7 | 3→5→4 |
| Intel | IMMEDIATE | 1920x1080 | 120 | 114.7 / 119.5 / 120.0 | 12.68 / 8.34 / 8.34 | 93 / 100 / 100 | 16.7 / 16.7 | 3→5→4 |
| Intel | IMMEDIATE | 1920x1080 | 144 | 143.1 / 144.0 / 144.0 | 7.08 / 6.95 / 6.95 | 99 / 100 / 100 | 20.9 / 20.8 | 3→5→4 |
| Intel | IMMEDIATE | fullscreen | 0 | 126.9 / 2135.9 / 1989.4 | 14.12 / 1.29 / 2.04 | 86 / 0 / 0 | 1.4 / 1.5 | 3→5→4 |
| Intel | IMMEDIATE | fullscreen | 60 | 60.0 / 60.0 / 60.0 | 16.67 / 16.67 / 16.67 | 0 / 0 / 0 | 16.8 / 16.7 | 3→5→4 |
| Intel | IMMEDIATE | fullscreen | 120 | 120.0 / 120.0 / 120.0 | 8.34 / 8.34 / 8.33 | 100 / 100 / 100 | 16.7 / 16.7 | 3→5→4 |
| Intel | IMMEDIATE | fullscreen | 144 | 139.3 / 144.0 / 144.0 | 13.86 / 6.95 / 6.95 | 97 / 100 / 100 | 13.9 / 13.9 | 3→5→4 |

## Not measured

- Hardware other than this laptop: AMD, a desktop with the panel on the
  rendering GPU, X11, Windows, VRR panels, refresh rates other than 144 Hz.
- `FIFO_RELAXED` and MAILBOX.
- The one-shallower bound live: the FIFO model isolates it; the live
  comparison against `main` also changes the image count.
- A locked session, and a window on another virtual desktop.
- Debug builds after the simplification.
