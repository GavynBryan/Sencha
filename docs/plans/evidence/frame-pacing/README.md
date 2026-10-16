# Frame pacing: measurement evidence

Measurements behind `docs/renderer/frame.md` (Pacing): why the engine held a
near-empty map at a fraction of the display's refresh rate, and what the
present-wait, image-count and display-queue changes did about it.

## Machine and method

- Laptop: RTX 4060 Laptop GPU plus Intel Raptor Lake-P graphics, KWin Wayland,
  internal panel 1920x1080 at 144 Hz, no VRR. The panel hangs off the Intel
  GPU, so every NVIDIA present crosses GPUs (PRIME) before the compositor
  shows it. Device index `0` is Intel; `auto` picks NVIDIA.
- Scene: `templates/fps`, `levels/room_2` (a near-empty FPS-template room).
- Builds: `before` is origin/main 8f13cb5e; `after` is the frame-pacing
  branch at 285751d0. `Release` is the `profile` preset; `Debug` is the `dev`
  preset run with `SENCHA_VALIDATION=1`.
- Runner: `scripts/bench_frame_pacing.sh` (900 frames per run, saved settings
  ignored, cells named mode x size x `r.target_fps` x device), reduced with
  `scripts/frame_pacing_stats.py` after dropping 120 warm-up frames; repeated
  runs of a cell are pooled. Windowed sizes are requested through
  `SENCHA_WINDOW_SIZE`; KWin trims 1920x1080 windowed to 1908x1042.
- Load: the owner's editor ran throughout (load average 2.2 to 3.0 before each
  run, recorded per cell); no compiler was running.

Columns: `fps` is frame starts per second; `med`/`p95`/`p99` are the frame
interval in ms; `@1T%` is the share of intervals within a quarter of one
refresh (6.94 ms). The wait and work columns are per-frame medians in ms:
`busy` CPU work no wait explains, `fence` the GPU frame-slot wait,
`pres_wait` the display queue hold, `acquire`, `limiter` the `r.target_fps`
hold, `gpu` the main colour pass on the GPU. `depth` is the display queue
limit, `img` the swapchain image count, `pres_lat` queue-to-display and
`frame_lat` frame-start-to-display latency. `before` builds report neither
latency nor depth; their image count was `minImageCount` (Intel 3, NVIDIA 2).

## Root causes

1. **Every frame waited for the display, in every present mode.** `before`
   blocked each frame until the present two frames back had been displayed
   (`vkWaitForPresentKHR`, no timeout). On this machine a present takes 20 to
   28 ms to reach the display from NVIDIA and about two refreshes from Intel
   windowed, so the wait made the display path's latency the frame interval:
   NVIDIA held at 96 fps (two of every three refreshes) at 720p and 48 fps
   (one in three) fullscreen, and IMMEDIATE and MAILBOX were held at the same
   rates. An instrumented `before` build with the wait depth and image count as
   knobs (NVIDIA, 720p, Release) isolates
   it: FIFO with the wait at depth 2 ran 96.2 fps, with no wait or depth 3
   ran 144; IMMEDIATE ran 96 with the wait and 183 to 218 without.
2. **The application could own one swapchain image.** `before` asked for
   `minImageCount` images, so the next frame could not record until the
   display returned the last one's image. On the NVIDIA path fullscreen the
   frame spent 20.5 ms of its 20.8 ms in acquire.

## Before and after

| build | cfg | GPU | mode | size | limit | runs | fps | med | p95 | p99 | @1T% | busy | fence | pres_wait | acquire | limiter | gpu | depth | img | pres_lat | frame_lat | load |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| before | Release | Intel | FIFO | 1280x720 | 0 | 2 | 144.0 | 6.94 | 7.36 | 7.54 | 100 | - | - | - | 0.02 | - | 0.36 | - | - | - | - | 2.5-2.6 |
| after | Release | Intel | FIFO | 1280x720 | 0 | 2 | 144.0 | 6.94 | 7.24 | 7.49 | 100 | 0.18 | 0.00 | 6.75 | 0.02 | 0.00 | 0.36 | 2 | 4 | 17.1 | 17.4 | 2.7-2.8 |
| before | Release | Intel | FIFO | 1280x720 | 120 | 1 | 120.0 | 8.33 | 8.33 | 8.33 | 100 | - | - | - | 0.01 | - | 0.36 | - | - | - | - | 2.6-2.6 |
| after | Release | Intel | FIFO | 1280x720 | 120 | 1 | 120.0 | 8.33 | 8.33 | 8.33 | 100 | 0.17 | 0.00 | 0.00 | 0.01 | 8.15 | 0.36 | 2 | 4 | 16.6 | 16.7 | 2.4-2.4 |
| before | Release | Intel | FIFO | 1280x720 | 60 | 1 | 60.0 | 16.67 | 16.67 | 16.67 | 0 | - | - | - | 0.02 | - | 0.36 | - | - | - | - | 2.6-2.6 |
| after | Release | Intel | FIFO | 1280x720 | 60 | 1 | 60.0 | 16.67 | 16.67 | 16.67 | 0 | 0.24 | 0.01 | 0.00 | 0.02 | 16.40 | 0.36 | 2 | 4 | 16.6 | 16.8 | 2.5-2.5 |
| before | Release | Intel | FIFO | 1920x1080 | 0 | 2 | 135.0 | 6.95 | 13.81 | 14.02 | 93 | - | - | - | 0.02 | - | 0.77 | - | - | - | - | 2.9-2.9 |
| after | Release | Intel | FIFO | 1920x1080 | 0 | 2 | 144.0 | 6.95 | 7.15 | 7.29 | 100 | 0.20 | 0.00 | 6.72 | 0.02 | 0.00 | 0.77 | 3 | 4 | 20.6 | 20.8 | 2.5-2.9 |
| before | Debug | Intel | FIFO | fullscreen | 0 | 2 | 144.0 | 6.94 | 7.15 | 7.27 | 100 | - | - | - | 0.03 | - | 0.80 | - | - | - | - | 2.1-2.1 |
| after | Debug | Intel | FIFO | fullscreen | 0 | 2 | 141.9 | 6.94 | 7.71 | 10.08 | 97 | 1.48 | 0.09 | 5.27 | 0.05 | 0.00 | 0.80 | 3 | 4 | 20.2 | 20.9 | 2.1-2.8 |
| before | Release | Intel | FIFO | fullscreen | 0 | 2 | 144.0 | 6.94 | 7.11 | 7.20 | 100 | - | - | - | 0.02 | - | 0.80 | - | - | - | - | 2.6-2.6 |
| after | Release | Intel | FIFO | fullscreen | 0 | 2 | 143.9 | 6.95 | 7.11 | 7.22 | 99 | 0.17 | 0.00 | 6.74 | 0.02 | 0.00 | 0.80 | 4 | 4 | 27.6 | 27.8 | 2.9-3.0 |
| before | Debug | Intel | FIFO | fullscreen | 120 | 2 | 119.5 | 8.33 | 8.33 | 8.40 | 99 | - | - | - | 0.02 | - | 0.80 | - | - | - | - | 2.1-2.2 |
| after | Debug | Intel | FIFO | fullscreen | 120 | 2 | 120.0 | 8.33 | 8.34 | 8.34 | 100 | 0.82 | 0.13 | 0.00 | 0.02 | 7.37 | 0.80 | 3 | 4 | 16.4 | 17.1 | 3.3-3.5 |
| before | Release | Intel | FIFO | fullscreen | 120 | 2 | 120.0 | 8.33 | 8.33 | 8.33 | 100 | - | - | - | 0.02 | - | 0.80 | - | - | - | - | 2.6-2.7 |
| after | Release | Intel | FIFO | fullscreen | 120 | 2 | 120.0 | 8.33 | 8.33 | 8.34 | 100 | 0.15 | 0.00 | 0.00 | 0.01 | 8.16 | 0.80 | 3 | 4 | 16.6 | 16.7 | 2.4-2.5 |
| before | Release | Intel | IMMEDIATE | 1280x720 | 0 | 2 | 144.0 | 6.94 | 7.20 | 7.44 | 100 | - | - | - | 0.02 | - | 0.36 | - | - | - | - | 2.7-2.9 |
| after | Release | Intel | IMMEDIATE | 1280x720 | 0 | 2 | 5284.0 | 0.17 | 0.35 | 0.60 | 0 | 0.04 | 0.12 | 0.00 | 0.01 | 0.00 | 0.10 | 2 | 4 | 0.7 | 0.7 | 2.9-2.9 |
| before | Release | Intel | IMMEDIATE | 1280x720 | 120 | 1 | 120.0 | 8.33 | 8.33 | 8.33 | 100 | - | - | - | 0.01 | - | 0.36 | - | - | - | - | 2.4-2.4 |
| after | Release | Intel | IMMEDIATE | 1280x720 | 120 | 1 | 120.0 | 8.33 | 8.33 | 8.33 | 100 | 0.16 | 0.00 | 0.00 | 0.01 | 8.16 | 0.36 | 2 | 4 | 16.6 | 16.7 | 2.4-2.4 |
| before | Release | Intel | IMMEDIATE | 1280x720 | 60 | 1 | 60.0 | 16.67 | 16.67 | 16.67 | 0 | - | - | - | 0.02 | - | 0.36 | - | - | - | - | 2.6-2.6 |
| after | Release | Intel | IMMEDIATE | 1280x720 | 60 | 1 | 60.0 | 16.67 | 16.67 | 16.67 | 0 | 0.22 | 0.01 | 0.00 | 0.02 | 16.42 | 0.36 | 2 | 4 | 16.6 | 16.8 | 2.4-2.4 |
| before | Release | Intel | IMMEDIATE | 1920x1080 | 0 | 2 | 133.5 | 6.95 | 13.86 | 13.97 | 83 | - | - | - | 0.02 | - | 0.77 | - | - | - | - | 2.4-2.6 |
| after | Release | Intel | IMMEDIATE | 1920x1080 | 0 | 2 | 2991.2 | 0.30 | 0.53 | 1.54 | 0 | 0.07 | 0.23 | 0.00 | 0.01 | 0.00 | 0.22 | 2 | 4 | 1.2 | 1.2 | 2.6-2.6 |
| before | Debug | Intel | IMMEDIATE | fullscreen | 0 | 2 | 144.0 | 6.94 | 7.19 | 7.37 | 100 | - | - | - | 0.03 | - | 0.80 | - | - | - | - | 2.0-2.1 |
| after | Debug | Intel | IMMEDIATE | fullscreen | 0 | 2 | 693.8 | 0.96 | 4.71 | 5.98 | 2 | 0.42 | 0.11 | 0.00 | 0.34 | 0.00 | 0.31 | 2 | 4 | 2.3 | 3.6 | 3.2-3.2 |
| before | Release | Intel | IMMEDIATE | fullscreen | 0 | 2 | 150.1 | 6.94 | 7.08 | 7.17 | 93 | - | - | - | 0.02 | - | 0.80 | - | - | - | - | 2.4-2.5 |
| after | Release | Intel | IMMEDIATE | fullscreen | 0 | 2 | 739.7 | 0.61 | 6.04 | 6.97 | 6 | 0.11 | 0.00 | 0.00 | 0.45 | 0.00 | 0.22 | 2 | 4 | 1.3 | 2.0 | 2.6-2.6 |
| before | Debug | Intel | IMMEDIATE | fullscreen | 120 | 2 | 120.0 | 8.33 | 8.33 | 8.34 | 100 | - | - | - | 0.02 | - | 0.80 | - | - | - | - | 2.0-2.3 |
| after | Debug | Intel | IMMEDIATE | fullscreen | 120 | 2 | 120.0 | 8.33 | 8.33 | 8.34 | 100 | 0.69 | 0.11 | 0.00 | 0.02 | 7.55 | 0.80 | 2 | 4 | 16.4 | 17.1 | 3.0-3.2 |
| before | Release | Intel | MAILBOX | 1280x720 | 0 | 1 | 107.8 | 7.05 | 13.98 | 14.17 | 66 | - | - | - | 0.02 | - | 0.36 | - | - | - | - | 3.0-3.0 |
| after | Release | Intel | MAILBOX | 1280x720 | 0 | 1 | 5320.5 | 0.17 | 0.35 | 0.61 | 0 | 0.06 | 0.10 | 0.00 | 0.01 | 0.00 | 0.10 | 2 | 4 | 0.7 | 0.7 | 2.7-2.7 |
| before | Release | Intel | MAILBOX | fullscreen | 0 | 1 | 214.8 | 5.36 | 7.03 | 7.12 | 64 | - | - | - | 0.02 | - | 0.80 | - | - | - | - | 2.8-2.8 |
| after | Release | Intel | MAILBOX | fullscreen | 0 | 1 | 948.1 | 0.56 | 4.72 | 5.87 | 2 | 0.11 | 0.00 | 0.00 | 0.42 | 0.00 | 0.27 | 2 | 4 | 1.3 | 2.2 | 2.6-2.6 |
| before | Debug | NVIDIA | FIFO | 1280x720 | 0 | 2 | 93.2 | 10.85 | 14.01 | 15.91 | 27 | - | - | - | 0.03 | - | 0.15 | - | - | - | - | 2.6-2.8 |
| after | Debug | NVIDIA | FIFO | 1280x720 | 0 | 2 | 144.0 | 6.95 | 7.46 | 7.89 | 100 | 0.56 | 0.00 | 6.30 | 0.02 | 0.00 | 0.15 | 4 | 3 | 27.2 | 27.8 | 2.4-2.5 |
| before | Release | NVIDIA | FIFO | 1280x720 | 0 | 2 | 96.2 | 10.39 | 13.88 | 14.06 | 47 | - | - | - | 0.02 | - | 0.15 | - | - | - | - | 2.3-2.5 |
| after | Release | NVIDIA | FIFO | 1280x720 | 0 | 2 | 144.0 | 6.94 | 7.16 | 7.31 | 100 | 0.18 | 0.00 | 6.74 | 0.02 | 0.00 | 0.15 | 4 | 3 | 27.6 | 27.8 | 2.6-2.9 |
| before | Debug | NVIDIA | FIFO | 1280x720 | 120 | 2 | 88.3 | 11.17 | 18.07 | 19.53 | 26 | - | - | - | 0.03 | - | 0.15 | - | - | - | - | 2.8-2.8 |
| after | Debug | NVIDIA | FIFO | 1280x720 | 120 | 2 | 120.0 | 8.33 | 8.34 | 8.34 | 100 | 0.69 | 0.12 | 0.00 | 0.02 | 7.49 | 0.15 | 4 | 3 | 24.6 | 25.3 | 2.4-2.5 |
| before | Release | NVIDIA | FIFO | 1280x720 | 120 | 2 | 96.0 | 10.32 | 12.68 | 12.93 | 49 | - | - | - | 0.02 | - | 0.15 | - | - | - | - | 2.5-2.6 |
| after | Release | NVIDIA | FIFO | 1280x720 | 120 | 2 | 120.0 | 8.33 | 8.33 | 8.33 | 100 | 0.15 | 0.00 | 0.00 | 0.01 | 8.17 | 0.15 | 4 | 3 | 24.9 | 25.0 | 2.7-3.0 |
| before | Release | NVIDIA | FIFO | 1280x720 | 144 | 2 | 96.0 | 10.32 | 13.88 | 14.04 | 48 | - | - | - | 0.02 | - | 0.15 | - | - | - | - | 2.8-3.0 |
| after | Release | NVIDIA | FIFO | 1280x720 | 144 | 2 | 143.3 | 6.94 | 6.95 | 7.72 | 99 | 0.16 | 0.00 | 3.11 | 0.02 | 2.21 | 0.15 | 4 | 3 | 27.8 | 27.9 | 2.9-3.0 |
| before | Release | NVIDIA | FIFO | 1280x720 | 60 | 2 | 60.0 | 16.67 | 16.67 | 16.67 | 0 | - | - | - | 0.02 | - | 0.15 | - | - | - | - | 2.5-2.6 |
| after | Release | NVIDIA | FIFO | 1280x720 | 60 | 2 | 60.0 | 16.67 | 16.67 | 16.67 | 0 | 0.21 | 0.00 | 0.00 | 0.02 | 16.44 | 0.15 | 2 | 3 | 16.6 | 16.8 | 2.2-2.4 |
| before | Release | NVIDIA | FIFO | 1920x1080 | 0 | 2 | 84.0 | 12.36 | 17.77 | 20.87 | 23 | - | - | - | 0.02 | - | 0.32 | - | - | - | - | 2.7-2.8 |
| after | Release | NVIDIA | FIFO | 1920x1080 | 0 | 2 | 119.9 | 8.88 | 11.39 | 11.56 | 48 | 0.19 | 0.00 | 0.00 | 7.18 | 0.00 | 0.31 | 5 | 3 | 34.2 | 41.7 | 2.6-2.7 |
| before | Debug | NVIDIA | FIFO | fullscreen | 0 | 4 | 46.4 | 20.84 | 27.84 | 27.96 | 0 | - | - | - | 19.56 | - | 0.33 | - | - | - | - | 2.5-2.9 |
| after | Debug | NVIDIA | FIFO | fullscreen | 0 | 4 | 85.5 | 13.79 | 20.93 | 21.12 | 41 | 0.90 | 0.00 | 0.00 | 12.68 | 0.00 | 0.33 | 4 | 3 | 21.7 | 35.2 | 2.4-2.8 |
| before | Release | NVIDIA | FIFO | fullscreen | 0 | 2 | 48.2 | 20.83 | 22.10 | 27.83 | 0 | - | - | - | 20.49 | - | 0.33 | - | - | - | - | 2.7-2.8 |
| after | Release | NVIDIA | FIFO | fullscreen | 0 | 2 | 87.9 | 13.83 | 14.07 | 20.88 | 40 | 0.18 | 0.00 | 0.00 | 13.63 | 0.00 | 0.33 | 4 | 3 | 21.0 | 34.8 | 2.9-3.0 |
| before | Debug | NVIDIA | FIFO | fullscreen | 120 | 4 | 46.3 | 20.85 | 27.84 | 27.96 | 0 | - | - | - | 19.56 | - | 0.33 | - | - | - | - | 2.3-3.0 |
| after | Debug | NVIDIA | FIFO | fullscreen | 120 | 4 | 86.4 | 12.37 | 19.40 | 19.60 | 42 | 0.90 | 0.00 | 0.00 | 11.40 | 0.00 | 0.33 | 4 | 3 | 22.8 | 35.2 | 2.6-2.9 |
| before | Release | NVIDIA | FIFO | fullscreen | 120 | 2 | 48.2 | 20.83 | 21.04 | 27.81 | 0 | - | - | - | 20.49 | - | 0.33 | - | - | - | - | 2.9-3.1 |
| after | Release | NVIDIA | FIFO | fullscreen | 120 | 2 | 85.4 | 12.43 | 19.45 | 19.58 | 42 | 0.22 | 0.00 | 0.00 | 12.21 | 0.00 | 0.33 | 4 | 3 | 22.4 | 34.8 | 2.5-2.5 |
| before | Release | NVIDIA | IMMEDIATE | 1280x720 | 0 | 2 | 97.1 | 8.72 | 13.93 | 14.07 | 49 | - | - | - | 0.02 | - | 0.15 | - | - | - | - | 2.5-2.9 |
| after | Release | NVIDIA | IMMEDIATE | 1280x720 | 0 | 2 | 172.8 | 5.66 | 9.35 | 10.44 | 45 | 0.15 | 0.00 | 0.00 | 5.49 | 0.00 | 0.14 | 2 | 3 | 24.9 | 29.9 | 2.9-2.9 |
| before | Release | NVIDIA | IMMEDIATE | 1280x720 | 120 | 2 | 97.1 | 8.83 | 12.62 | 12.94 | 50 | - | - | - | 0.02 | - | 0.15 | - | - | - | - | 2.5-2.5 |
| after | Release | NVIDIA | IMMEDIATE | 1280x720 | 120 | 2 | 120.0 | 8.33 | 8.33 | 8.33 | 100 | 0.16 | 0.00 | 0.00 | 0.01 | 8.16 | 0.15 | 2 | 3 | 24.9 | 25.1 | 2.8-2.9 |
| before | Release | NVIDIA | IMMEDIATE | 1280x720 | 144 | 2 | 97.7 | 8.06 | 13.82 | 13.95 | 51 | - | - | - | 0.02 | - | 0.15 | - | - | - | - | 2.5-2.5 |
| after | Release | NVIDIA | IMMEDIATE | 1280x720 | 144 | 2 | 144.0 | 6.94 | 6.95 | 6.95 | 100 | 0.15 | 0.00 | 0.00 | 0.01 | 6.78 | 0.15 | 2 | 3 | 20.8 | 20.9 | 2.6-2.8 |
| before | Release | NVIDIA | IMMEDIATE | 1280x720 | 60 | 2 | 60.0 | 16.67 | 16.67 | 16.67 | 0 | - | - | - | 0.02 | - | 0.15 | - | - | - | - | 2.7-3.0 |
| after | Release | NVIDIA | IMMEDIATE | 1280x720 | 60 | 2 | 60.0 | 16.67 | 16.67 | 16.67 | 0 | 0.21 | 0.00 | 0.00 | 0.02 | 16.43 | 0.15 | 2 | 3 | 33.2 | 33.4 | 3.0-3.0 |
| before | Release | NVIDIA | IMMEDIATE | 1920x1080 | 0 | 2 | 84.4 | 11.99 | 17.69 | 20.74 | 19 | - | - | - | 0.02 | - | 0.32 | - | - | - | - | 2.7-2.7 |
| after | Release | NVIDIA | IMMEDIATE | 1920x1080 | 0 | 2 | 90.5 | 10.93 | 16.11 | 17.03 | 23 | 0.19 | 0.00 | 0.00 | 10.71 | 0.00 | 0.32 | 2 | 3 | 39.7 | 50.8 | 2.8-2.9 |
| before | Debug | NVIDIA | IMMEDIATE | fullscreen | 0 | 2 | 57.2 | 16.39 | 27.72 | 28.31 | 1 | - | - | - | 3.53 | - | 0.33 | - | - | - | - | 2.2-2.3 |
| after | Debug | NVIDIA | IMMEDIATE | fullscreen | 0 | 2 | 85.9 | 13.78 | 20.92 | 21.05 | 42 | 0.96 | 0.00 | 0.00 | 12.57 | 0.00 | 0.33 | 2 | 3 | 21.7 | 35.3 | 2.5-2.7 |
| before | Release | NVIDIA | IMMEDIATE | fullscreen | 0 | 2 | 48.2 | 20.83 | 20.99 | 21.23 | 0 | - | - | - | 20.55 | - | 0.33 | - | - | - | - | 2.2-2.3 |
| after | Release | NVIDIA | IMMEDIATE | fullscreen | 0 | 2 | 75.0 | 13.87 | 20.92 | 21.01 | 30 | 0.22 | 0.00 | 0.00 | 13.64 | 0.00 | 0.25 | 2 | 3 | 27.9 | 41.7 | 2.6-2.7 |
| before | Release | NVIDIA | MAILBOX | 1280x720 | 0 | 1 | 96.8 | 10.00 | 13.91 | 13.99 | 48 | - | - | - | 0.01 | - | 0.15 | - | - | - | - | 3.0-3.0 |
| after | Release | NVIDIA | MAILBOX | 1280x720 | 0 | 1 | 170.0 | 5.71 | 10.24 | 10.53 | 45 | 0.15 | 0.00 | 0.00 | 5.50 | 0.00 | 0.14 | 2 | 3 | 24.3 | 29.5 | 2.8-2.8 |
| before | Release | NVIDIA | MAILBOX | fullscreen | 0 | 1 | 71.7 | 13.50 | 17.94 | 18.44 | 1 | - | - | - | 1.58 | - | 0.33 | - | - | - | - | 2.9-2.9 |
| after | Release | NVIDIA | MAILBOX | fullscreen | 0 | 1 | 74.6 | 13.88 | 20.91 | 21.01 | 30 | 0.21 | 0.00 | 0.00 | 13.66 | 0.00 | 0.25 | 2 | 3 | 27.9 | 41.7 | 2.7-2.7 |

## Reading the table

- **Intel, the path that is not externally limited**: Release FIFO holds
  144 Hz at every size with p99 at or under 7.5 ms; Debug fullscreen held it in
  one run and dropped to 139.8 fps in the other (next section). Vsync off reaches 560 to 5300 fps.
  `before` already held 144 in FIFO except windowed 1080p (135 fps), but
  capped IMMEDIATE and MAILBOX at refresh.
- **NVIDIA at 720p** goes from 96 to 144 fps in FIFO and from 97 to 170 fps
  with vsync off.
- **NVIDIA at 1080p and fullscreen stays below refresh**: FIFO 85 to 120 fps,
  vsync off 75 to 90 fps, against 48 to 84 before. The frame spends 11 to
  14 ms in acquire: the cross-GPU path returns images more slowly than the
  display takes them. `vkcube` on the same path (FIFO, 1440 frames, 1920x1080
  windowed) takes 17.5 s on NVIDIA and 10.1 s on Intel, about 82 and 142 fps,
  so the ceiling is the platform's. One more swapchain image raised NVIDIA
  fullscreen FIFO from 88 to 109 fps in the knob build; windowed 1080p did not
  move with one to three more. Intervals alternate between two and three
  refreshes there (`@1T%` 40), which is uneven, not smooth.
- **Limits**: `r.target_fps` 60 and 120 hold their rate exactly (p99 within
  0.01 ms) in both modes and on both GPUs; under FIFO a 120 limit on the
  144 Hz panel is shown at the uneven 5-in-6 cadence `frame.md` describes.
- **Simulation**: 59.4 to 62.3 fixed ticks per second of wall time at the
  60 Hz tick in every cell (`tick/s` in the raw summaries), whatever the
  frame rate.
- **Latency**: `after` reports 13 to 28 ms queue-to-display at 144 Hz. The
  display queue limit settles at 2 to 4 within the first 25 frames, and higher
  than it needs to (next section).

## The display queue limit deepens beyond need

`DisplayQueueLimit` deepens on two starved refreshes within a second and never
comes back until the swapchain is rebuilt. Measured:

- Two identical Intel 720p FIFO runs settled at depth 3 and depth 2, both at
  144 fps with every interval on one refresh; the depth-3 run carries 7 ms
  more latency (20.5 against 13.7 ms). Its step came at frame 8, during the
  window's first presents, where both runs show the same alternating 7/14 ms
  intervals.
- An Intel fullscreen run held 144 Hz at depth 3 for 300 frames, then one
  disturbance (two consecutive late presents, frames 311 and 312) moved it to
  4 for the rest of the run: 20.6 to 27.5 ms. `before` held the same cell at
  144 Hz with the depth fixed at 2.
- In one Debug fullscreen Intel run the limit reached 4, deeper than the four
  images let the path queue, so the hold stopped binding and acquire and
  present did instead (median acquire 5 ms, after input), with 25 to 49 ms
  stalls inside `vkQueuePresentKHR` and `vkAcquireNextImageKHR`: 139.8 fps,
  p99 17 ms. The run before it stayed at depth 3: 144.0 fps, p99 7.9 ms.
- Over a 25 000-frame Intel 720p windowed session (about 3 minutes) the
  limit crept to 7: 144 fps, p99 7.8 ms, queue-to-display 34.9 ms, against
  13.7 ms at depth 2. An NVIDIA fullscreen session of the same length stayed
  at 4.

## Not measured

- Hardware other than this laptop: AMD, a desktop with the panel on the
  rendering GPU, X11, Windows, VRR panels, refresh rates other than 144 Hz.
- `FIFO_RELAXED`.
- Windowed 1080p with a frame-rate limit, and Debug at windowed 1080p.
- MAILBOX in Debug.
- A hidden or locked display (the hold's timeout is covered by a unit test,
  not exercised live).
- `after` at HEAD after the hold policy moved into `DisplayQueueLimit::HoldFor`:
  the tables use a build of 285751d0; a Debug build of the refactor was run at
  720p on both GPUs and matched (Intel FIFO 143.0 fps at depth 2, NVIDIA FIFO
  144.0 at depth 4).
