# Contract cards

One page per engine module in `engine/include/` and `engine/src/`: when it runs, what it reads and writes, where it decides, what its tests guarantee, how big it is, and where `docs/` disagrees with the code.

Every claim cites a file or a test. Cards describe the tree at the commit named in their header; regenerate the size and complexity lines with `lizard -l cpp engine/include/<module> engine/src/<module>` when you edit one.

| Layer | Cards |
|---|---|
| Foundation | [core](core.md), [math](math.md), [ecs](ecs.md), [jobs](jobs.md), [platform](platform.md), [time](time.md), [components](components.md), [gameplay_tags](gameplay_tags.md), [profiling](profiling.md) |
| World and frame | [app](app.md), [runtime](runtime.md), [world](world.md), [zone](zone.md), [assets](assets.md) |
| Gameplay simulation | [abilities](abilities.md), [attributes](attributes.md), [effects](effects.md), [authored](authored.md), [logic](logic.md), [projectile](projectile.md), [perception](perception.md), [attach](attach.md) |
| Motion and space | [movement](movement.md), [controller](controller.md), [camera](camera.md), [input](input.md), [navigation](navigation.md), [spatial](spatial.md), [physics](physics.md), [anim](anim.md) |
| Network | [net](net.md), [prediction](prediction.md), [participant](participant.md), [participant_spawn](participant_spawn.md) |
| Presentation | [render](render.md), [graphics](graphics.md), [ui](ui.md), [audio](audio.md), [debug](debug.md), [presentation](presentation.md) |

Section 1 names the `FramePhase` and, for scheduled systems, the `EngineSchedule` lane that `engine/src/app/EngineFramePhases.cpp` runs it from. A module with no frame phase of its own says so and names who calls it.
