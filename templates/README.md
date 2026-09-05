# Starter templates

Each directory is a complete Sencha game project: a `CMakeLists.txt` that
builds a game module against an installed SDK, a `project.senchaproj` the
editor opens, the module's sources, and its authored content. Copy one to start
a game; nothing in a template refers to anything outside its own directory.

A template composes engine mechanisms and owns every policy. The engine boots,
mounts content, loads a level, spawns scenes, runs frames, and shuts down with
a game whose every hook is empty -- that game is `blank/`. Everything a
player-facing template has beyond that is code in the template, not a feature
the engine assumes.

- `blank/` -- an empty game module and one empty scene. Proof that the engine
  needs no game, and the place to start when none of the others is your game.
- `fps/` -- a first-person game: prefab pawn, mouse look, planar steering,
  jumping, a streamed world. The one template with world content. No
  networking: it pays for none.
- `arena/` -- the FPS with a session as its point: `host` and `connect`, every
  peer's pawn spawned by the authority from one prefab and predicted on its own
  machine, and a turret sample that proves possession over the wire. A copy of
  `fps/` by design; the two share by duplication.

In this repository the templates are also built in-tree, under
`SENCHA_BUILD_TEMPLATES`, so they rebuild with the engine.
