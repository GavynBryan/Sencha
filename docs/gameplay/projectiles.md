# Projectiles

A projectile is something launched that flies on its own and does something to
what it meets: a bolt, an arrow, a fireball, a mortar round, a thrown rock. It is
a prefab whose root carries `Projectile`; everything else it is -- its mesh, its
light, its sound -- is the rest of the prefab. Nothing about it assumes a gun, a
first-person camera, or any particular game.

| Piece | Owns |
|---|---|
| `Projectile` (`projectile/Projectile.h`) | how it flies and what a contact does, authored on the prefab root |
| `ProjectileSystem` (`projectile/ProjectileSystem.h`) | launching, flight, contacts, consequences on the authority; presentation elsewhere |
| `ProjectileFlight` (`projectile/ProjectileState.h`) | what every machine knows: instigator, launch tick, end tick |
| `ProjectileMotion` | the authority's working state while it flies; never leaves the authority |
| `projectile.launch` (`projectile/ProjectileVerbs.h`) | the authored way to launch one |
| `projectile.hit` (`projectile/ProjectileHit.h`) | the occurrence delivered at what it strikes |

## Authoring one

Make a prefab whose single root carries `Projectile` beside whatever it looks
like. Its fields:

| Field | Meaning |
|---|---|
| `speed`, `gravity_scale` | metres per second at launch; how much of the world's gravity bends its path (zero flies straight) |
| `radius` | the sphere it sweeps along its path; zero sweeps a ray |
| `lifetime` | seconds before it ends on its own |
| `max_hits`, `passes_through` | how many entities it may strike, at most 16; an entity carrying the `passes_through` tag is struck and passed through while hits remain, anything else stops it |
| `hit_effect`, `scaled_effect` | effects applied to each entity struck that has attributes; the scaled one at the launch's magnitude, skipped at zero |
| `splash_radius`, `splash_effect`, `splash_needs_sight`, `splash_on_expire` | an area around where it ends that its effects also reach -- the splash effect, or the hit effects when none is named -- optionally only in sight, optionally when its time runs out as well as when it strikes |
| `hit_kind` | a gameplay tag carried by `projectile.hit`, so responses tell one kind of shot from another |
| `impact_cue` | a cue appended to the instigator's cue log wherever it strikes or ends |
| `hits_characters`, `ends_with_instigator` | whether walking characters stop it; whether it ends when its instigator is gone |

Effects and tags are named, as `asset://effects/<name>.sdata` and tag names, and
resolved the first time the prefab is launched. A name nobody declared refuses
every launch of that prefab, with the prefab and the name in the log.

The root's mesh is drawn facing its path: +Z is forward, and its scale is the
prefab's own.

## Launching one

From content, `projectile.launch`: the instigator, the prefab as a Scene asset
(`{"asset": "asset://prefabs/<name>.smap"}`), then an optional origin and
direction (the instigator's aim when absent), a spread in degrees, a speed scale,
a magnitude, and a splash scale. From native code,
`ProjectileSystem::Launch(ProjectileLaunch)`. Both are the authority's only, and
both refuse rather than queue: at `projectile.capacity` in flight a launch is
`Full`, and a prefab that is not a projectile is `Unresolved`.

The prefab is a dependency of the content that names it
(`docs/gameplay/authored-api.md`, "The value contract"): an ability or cue
presentation naming a missing scene, or an asset of another kind, is reported when
the content is declared, and the scene is resident for as long as that content is
published, so no launch loads it. The binding names the prefab by id, so a launch
resolves no path. The limit is the World's: `ProjectileCapacity` beside
`ProjectilesInFlight`, so whatever admits a launch ahead of committing to it --
a phase's hold, before its cost and cooldown -- compares exactly what the launch
will.

A launch builds the prefab in the instigator's partition, so a projectile lives
and unloads with the zone it was fired in, places it at the origin facing its
scattered direction, and marks it for replication. Scatter is uniform over the
cone and chosen by the launch's seed alone; the verb seeds from the instigator,
the tick and which of that instigator's launches this tick it is
(`ProjectileSystem::NextSeed`), so two launches in one tick scatter apart, a
replay of the same launches scatters the same way, and no other instigator's
launches change it. The cone itself is `ScatterInCone`
(`math/geometry/3d/ConeScatter.h`), so a hitscan weapon's spread means exactly
what a projectile's does.

A projectile flies on the tick it is launched: the system runs after the ability
pass, where most launches come from, and before attributes resolve, so what it
strikes that tick is resolved that tick.

## Flight and contacts

Each tick the authority moves a flying projectile along the chord of its arc --
exact for straight flight, within a tick's curvature for a falling one -- and
sweeps its sphere along that chord against two things:

- physics bodies, through `PhysicsQueries::SweepShape`, ignoring sensors, its
  instigator and everything it has already struck;
- walking characters, whose `CharacterVirtual` capsules no physics query sees.
  Each `CharacterController` is an upright capsule about its entity's position,
  tested analytically.

Contacts are taken nearest first. Each is struck at most once in the
projectile's life: its effects are applied, `projectile.hit` is delivered at it,
and the impact cue is appended. Level geometry with no entity of its own is cued
and stops the projectile, and is told nothing. A projectile then passes through
what carries its `passes_through` tag while its hits last, and ends against
anything else.

A projectile ends where it stopped, or wherever it is when its lifetime runs
out. Ending splashes if it has a splash -- when it struck, or on expiry if it was
made to -- reaching, in identity order, the bodies and characters within the
radius, except its instigator and what it already struck, and only what has a
clear line from the end point when it needs sight.

### What a strike is not

Splash is not a strike: it applies effects and delivers nothing. A projectile's
consequences are effects, the delivery and the cue, and nothing else: damage is
an effect, death is the owning system's, and anything a struck entity does about
being struck is its responses' (`docs/gameplay/responses.md`).

`projectile.hit` carries `instigator`, `projectile`, `kind`, `point` and
`normal`, with the instigator as its instigator. A hitscan weapon that means the
same thing delivers the same `ProjectileHit` with no projectile, so a door that
opens to any shot answers one event.

## Across machines

The authority flies every projectile and decides every contact. Nothing on any
other machine moves one or applies anything.

- **The body.** A projectile is an ordinary replicated prefab group: its root
  carries `NetReplicated` and `NetSpawnPrefab`, so a client builds the same prefab
  and draws its own mesh. Only the root travels.
- **The pose.** Its `LocalTransform` replicates and is interpolated like any
  mirrored entity's, so a client draws it at the same moment in the past as
  everything around it -- which is what makes a projectile meet a moving target
  on screen where it met it on the authority.
- **The flight.** `ProjectileFlight` carries the instigator, mapped to the
  client's own entity, and the authority ticks it was launched and ended on. A
  client draws a mirrored projectile's meshes only while the tick it is
  presenting (`ReplicationInterpolation::PresentedAuthorityTick`) lies between
  the two, so it is not seen waiting at its muzzle before it was launched, nor
  resting at the wall after it struck. Without a clock estimate it is drawn while
  it has not ended.
- **The end.** An ended projectile is kept, hidden and inert, for
  `projectile.linger_ticks` before it goes, so that its end reaches every peer
  even when it lived for less than a snapshot interval.
- **Cues.** The impact cue rides the instigator's `AbilityCueLog`, which already
  replicates, so whatever presents an ability's cues presents a projectile's.

Never replicated: `ProjectileMotion` -- velocity, remaining life, what it has
struck. A client could not use it without second-guessing the authority.

### Not yet

- **A firer's own projectile is not predicted.** It appears on the firer's
  screen when the authority's does, a round trip and the interpolation delay
  after the input. A guess would be matched to the authority's projectile by its
  instigator and launch tick, which both already carry, but no machine guesses
  yet (`docs/deferred.md`).
- **No lag compensation.** What a projectile strikes is decided against the
  authority's present, not against what the firer saw.
- **No bounce, homing or attached flight.** A contact strikes, passes through,
  or stops.

## What it costs

A flying projectile costs one shape sweep against the physics world and one
capsule test per character per tick, plus a sweep per pass-through. A launch
builds a cached prefab package -- a decode of resident data, no file read -- and
the prefab is resolved once. Capacity is a cvar; the default is 256
(`kProjectileDefaultCapacity`).
