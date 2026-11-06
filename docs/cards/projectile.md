# projectile

Snapshot `97ed48e4` (2026-10-30). Layer: gameplay simulation.

## 1. Frame phases

- `Simulate` (FixedLogic lane): `ProjectileSystem::FixedLogic` (`engine/include/projectile/ProjectileSystem.h:145`) flies projectiles on the authority and presents mirrors elsewhere.
- `RegisterProjectileSystem` (`engine/src/app/Engine.cpp:1471`) is the engine's call site.

## 2. Inputs

- Launch requests, `ProjectileLaunch` (`engine/include/projectile/ProjectileSystem.h:57`), from the launch verb and abilities.
- Prefabs resolved once per World, `ProjectileDefinitions` (`engine/include/projectile/ProjectileSystem.h:49`).
- Physics queries and effect definitions, through `Services` (`engine/src/projectile/ProjectileSystem.cpp:830`).
- Module dependencies: abilities, app, assets, attributes, authored, controller, core, ecs, effects, gameplay_tags, logic, math, net, physics, render, world.

## 3. Outputs

- `ProjectileFlight` (`engine/include/projectile/ProjectileState.h:30`) and `ProjectileMotion` (`engine/include/projectile/ProjectileState.h:50`): in-flight state, replicated as `ProjectileFlightWire` (`engine/include/projectile/ProjectileFlightWire.h:10`) and drawn by the system's own `Present` (`engine/src/projectile/ProjectileSystem.cpp:717`).
- `ProjectileHit` (`engine/include/projectile/ProjectileHit.h:25`): the occurrence a struck entity hears, handed to responses by `Deliver` (`engine/src/projectile/ProjectileSystem.cpp:598`).
- Effects on what it strikes, through `ApplyEffect` (`engine/include/effects/EffectSystem.h:17`).

## 4. Central decisions

- `ProjectileSystem::Launch` (`engine/src/projectile/ProjectileSystem.cpp:279`): admits or refuses a launch against authority, capacity and the prefab.
- `ProjectileSystem::Fly` (`engine/src/projectile/ProjectileSystem.cpp:405`): moves one projectile a tick and decides what it strikes or passes through.
- `ProjectileSystem::Resolve` (`engine/src/projectile/ProjectileSystem.cpp:222`): resolves a prefab's names into a flight definition, or refuses it.

## 5. Invariants

- It never strikes its instigator: `ProjectileFixture.ItNeverStrikesItsInstigator` (`test/runtime/ProjectileTests.cpp`).
- It strikes what it meets once, applies its effects and stops: `ProjectileFixture.ItStrikesWhatItMeetsOnceAppliesItsEffectsAndStops` (`test/runtime/ProjectileTests.cpp`).
- A launch past capacity is refused until one ends: `ProjectileFixture.ALaunchPastCapacityIsRefusedUntilOneEnds` (`test/runtime/ProjectileTests.cpp`).
- Scatter is chosen by the seed alone and stays inside its cone: `ProjectileFixture.ScatterIsChosenByTheSeedAloneAndStaysInsideItsCone` (`test/runtime/ProjectileTests.cpp`).
- A machine that is not the authority never launches, flies or strikes: `ProjectileFixture.AMachineThatIsNotTheAuthorityNeverLaunchesFliesOrStrikes` (`test/runtime/ProjectileTests.cpp`).
- Unnamed fields load at their defaults: `ProjectileAuthoring.AProjectileNamingOnlySomeFieldsLoadsWithTheRestAtTheirDefaults` (`test/editor/ProjectileAuthoringTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 1,554 lines in 9 files.
- CCN 29 `ProjectileSystem::Launch` (`engine/src/projectile/ProjectileSystem.cpp:279`); CCN 26 `ProjectileSystem::Fly` (`engine/src/projectile/ProjectileSystem.cpp:405`); CCN 23 `ProjectileSystem::Resolve` (`engine/src/projectile/ProjectileSystem.cpp:222`).

## 7. Docs that contradict the code

- None found.
