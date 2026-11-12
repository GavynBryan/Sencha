# Skinned pose capacity

Skinning runs as a compute pre-skin: each posed instance gets its joint palette
uploaded and a dispatch that writes its posed vertices, which every geometry
pass then draws like static geometry. See `open-work.md` for why (Decision N).

## Budget

`EngineGraphicsConfig::SkinnedPoseDispatchesPerFrame` (default 128) and
`ScratchSkinningCapBytes` (default 1 MiB, 128 instances of up to 128 joints)
bound the work of one frame. `SelectSkinnedPoses` chooses within them:

1. Instances that have never had a pose, nearest first. A spawn burst of K
   instances clears in K / budget frames, rounded up.
2. One eighth of the budget for whoever has waited longest, so a distant
   instance keeps moving instead of freezing.
3. The rest nearest first. Ties break on `RenderEntityKey`.

128 is a starting budget, set from the Praying Man baseline on the Intel iGPU:
about 13 microseconds per instance on that GPU. Change the config rather than
the code, and re-measure before raising it.

## What an instance can be

Every visible instance is in exactly one state each frame, and the five sum to
the visible count. `RenderFrameHealth` and `RenderStats` report them.

| State | Meaning |
|---|---|
| First pose | Dispatched for the first time this frame |
| Refreshed | Dispatched, replacing an earlier pose |
| Held | Past the budget or failed; draws its latest finished pose |
| Awaiting first pose | Never posed and not chosen yet; draws at rest |
| Bind fallback | Chosen, could not be posed, and has no earlier pose |

Held and awaiting are the designed response to overload, not lost work. Bind
fallback and `SkinnedPoseFailures` are lost work and fail `LostWork()`.

## Buffer lifetime

An instance owns one posed buffer per frame in flight, F in total, created when
first needed. `PosedBufferVersions` decides which it may touch:

- When frame n records, frames at or before n - F have completed (the frame
  service waited on that slot's fence). At most F - 1 earlier frames are
  unfinished, and each references at most one version of an instance, so at most
  F - 1 of the F versions are busy and one is always free.
- A version is writable once every frame that touched it has retired. Reads
  stamp too: holding a pose re-stamps the version it reads, or a later frame
  would see it retired and overwrite it while this frame still draws from it.
- The latest finished pose is never the write target while another version is
  free. With one frame in flight it is the only version and nothing can be
  reading it.
- If nothing is writable the instance holds and the frame counts a pose failure.
  The argument above makes that unreachable unless the frame clock is behind.

The compute-to-vertex barrier recorded after a frame's dispatches also covers
later submissions on the queue, so reading an earlier frame's pose needs no
barrier of its own.

`PosedBufferVersionsTests` simulates a GPU that finishes frames at arbitrary
times, at 1 to 4 frames in flight.
