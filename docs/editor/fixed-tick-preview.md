# Fixed-tick previews

A workspace that previews runtime behaviour -- the Animation workspace's rig
scenarios, the Ability Workbench's fixture pawn -- runs the production systems in a
private `World` it owns, on a fixed tick, never the frame clock. The clock half of
that is one mechanism, `FixedTickPreview<Record>`
(`editor/common/src/preview/FixedTickPreview.h`); the World, its systems and what a
tick records stay with the workspace's session.

## What the clock owns

- **Tick zero.** `Restart` asks the session to rebuild its World, then runs tick zero.
  A session whose World cannot run (an unbound rig, an ability that did not compile)
  reports so, and the clock steps nothing until it is rebuilt.
- **Stepping and playing.** `Step` runs the next tick; `Advance(wallSeconds, rate)`
  runs as many as wall time and the speed schedule, at most eight a call, dropping
  rather than catching up after a stall. Speed changes how quickly ticks are
  scheduled, never their length, so a slowed preview produces the same ticks.
- **History.** Each tick's `Record` -- whatever the session chooses to keep -- goes
  into a bounded history (3600 ticks, a minute at 60 Hz) that panels and decks read.
- **Rewind is replay.** Running to an earlier tick rebuilds and runs forward from
  zero; `Replay` does the same back to the current tick after a change the past
  depends on (a declared tag, a role, a recorder). Nothing is snapshotted, so what a
  rewound preview shows is what the systems produce from the script, by construction.
- **Branching.** The session keeps a script of timed actions. An edit made at a past
  tick cuts the script after that tick and lands on the next one; several edits made
  while paused on one tick share one branch (`Branch` is true only for the first).

## What a session owns

Its World and teardown order, its systems and how a tick runs them, its script and
how an action applies, and its tick record. A session keeps the clock's step,
play and speed operations as its own public surface, so panels address one object.
