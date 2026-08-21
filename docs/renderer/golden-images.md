# Golden images

The renderer's only pixel-level coverage. Everything else in the suite runs
without a device, which is right for policy and blind to the thing a renderer
exists to produce.

## Why it exists

A whole class of defect leaves every counter correct, every assertion green, and
the validation layers silent, and shows up only in the image:

- a material setting parsed, validated, written, round-tripped, given an editor
  slider, and never reaching a shader (alpha masking, for months);
- two paths that should agree drifting apart (the background skipping the
  display transform the scene went through);
- a pass drawing the right geometry through the wrong state;
- a uniform range silently truncated so a shader reads a block that is not there.

Every one of those was found by a person looking at a capture, which means they
were found at whatever rate someone happened to look.

## What it does

`test/render_golden/GoldenImageTests.cpp` runs the shipped host against a scene,
captures one frame, and compares it byte for byte against a reference committed
under `test/render_golden/references/`.

The comparison is exact because the renderer is bit-deterministic frame to frame
on one build. That is measured, not assumed: three separate runs of the probe
scene produced byte-identical PNGs. Exactness is worth more than a tolerance
here — a threshold that hides a one-percent shading change hides the defects
this is for.

It **skips** without a display rather than failing, so a machine that cannot run
it says so instead of blocking. It needs no cooked content: the scene loads from
`template/assets` directly.

## When it fails

One of two things is true, and only a person can say which.

**Something broke.** Open the `.actual.png` written beside the reference and
compare. The failure message carries the differing-pixel count, which is a
decent first signal: a few pixels along an edge is not the same finding as four
percent of the frame.

**The change was meant to alter the image.** Then the reference is stale.
Replace it with the capture **in the same commit as the change that moved it**,
so the diff shows the picture changing next to the reason it changed. That
review — is this new image right? — is the ongoing cost of this mechanism, and
it is the whole point: it puts a person in front of the pixels exactly when the
pixels move, rather than months later.

Never widen a tolerance to make a failure go away. There is no tolerance.

## Capturing a frame by hand

The same mechanism, driven from the console:

```
render.screenshot <path.png> [frame]
```

With a frame number it waits until the renderer has drawn that many. The first
frames of a run are a window appearing, a swapchain being recreated, and assets
still arriving, so an unattended capture should name a frame the scene has
settled by. From a command line:

```sh
cd template && SENCHA_PRESENT_MODE=IMMEDIATE ../build/example/SceneViewer/app \
  +map levels/shadow_probe.level \
  +render.screenshot /tmp/frame.png 150 \
  +set app.exit_after_frames 300
```

Capture is unavailable when the surface does not offer `TRANSFER_SRC` usage on
its swapchain images; the command says so rather than failing quietly.

## Adding a scene

Add an entry to `GoldenImageTests.cpp`, run it once, look at the `.actual.png`
it reports, and commit it as the reference if it is right. **Look at it** — a
reference adopted without being read pins whatever was broken at the time.

The scene set is deliberately small. Each one costs about a second of test time
and a hundred kilobytes in the tree, and the coverage that matters is breadth of
*renderer features* in frame, not breadth of content. The first scene is framed
by the level's own spawn point and leaves much of the frame as background: it
covers the sky gradient, the forward pass, textured materials, light falloff,
and the display transform, and a better-framed view of the same scene would
cover more per pixel. That is the obvious next improvement.
