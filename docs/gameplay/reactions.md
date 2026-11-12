# Effect reactions

An entity can answer the effects applied to it: a push, a shake of the view it
owns, in proportion to how far the effect moved one of its attributes, and a
flinch.
A reaction is keyed to the kind of effect and the change it made, never to the
ability or projectile that delivered it, so a new attack reacts as soon as its
effect carries a kind a profile answers.

The engine knows nothing of damage or health. It knows that an effect was
applied from a source, that it carries kind tags, and that it changed named
attributes by measured amounts. Which attribute matters and which kinds exist
are the game's.

| Piece | Where | What it is |
| --- | --- | --- |
| `EffectSource` | `effects/EffectSource.h` | who applied an effect, and the strike's point and direction when it had one |
| Kinds | `kinds` on `effect.definition` | classification tags; never granted to the target |
| `EffectApplicationLog` | `effects/EffectApplicationLog.h` | each application that changed Base this tick, with each attribute's Base before and after, clamped |
| `effect.reaction_profile` | `logic/EffectReactionProfile.h` | the reactions, as data |
| `EffectReactions {profile}` | `logic/EffectReactions.h` | the component that points an entity at its profile |
| `EffectReactionSystem` | `logic/EffectReactionSystem.h` | reads the log, answers it, clears it |
| `movement.impulse` | `movement/MovementVerbs.h` | the same push, for content |

## Where an application comes from

`ApplyEffect` takes an optional `EffectSource`. A projectile fills it from its
strike: the instigator, the point and the direction it travelled; a splash
gives its centre. `effect.apply` takes optional `instigator` and `from`
arguments. Abilities' costs and cooldowns give none.

## What is recorded

While a World holds an `EffectApplicationLog`, every application that changes
Base is recorded: an instant effect landing, and a periodic effect's period
falling due. A lasting effect that folds into Current is not, since Base never
moves. Each record carries the target, the effect, its source and scale, and,
for each attribute the effect modifies, Base as the application found it and
as it left it after clamping. A blow for 40 on a target with 5 left records 5.
A hit scale query's answer is already in the change, so a headshot that hurts
more pushes more with no rule of its own.

`EffectReactionSystem` adds the log, reads it once a tick on the authority
after every engine pass that applies effects (projectiles, abilities,
responses, the lifetime pass), and clears it. An effect applied later in the
tick is answered on the next.

## A profile

```json
{ "type": "effect.reaction_profile", "version": 1, "data": { "reactions": [
  { "kinds": { "any": ["lf.damage.claw"] },
    "recipient": { "none": ["lf.state.dead"] },
    "attribute": "Health", "change": "decrease", "threshold": 1,
    "impulse": { "per_unit": 0.18, "max": 4, "lift": 0.1 },
    "shake": { "profile": "asset://camera/struck.sdata", "per_unit": 0.03, "max": 1 },
    "animation": { "front": "Anim.Flinch.Front", "back": "Anim.Flinch.Back", "ticks": 24 } } ] } }
```

- The first reaction that matches an application answers it. A reaction
  matches when the effect's kinds pass `kinds`, the recipient's tags pass
  `recipient`, and the named attribute moved the way `change` says by at least
  `threshold`. An empty query matches anything.
- The push is `per_unit` metres per second for each unit of change, capped at
  `max`, plus `lift` of that straight up. It runs along the strike's direction,
  else away from its point, else away from the instigator, flattened onto the
  ground plane. With none of those it pushes nothing and still shakes.
- The push goes through `AddMotionImpulse`, so it adds to whatever the
  character was moving at and the character slows from it as from any other
  motion. An entity without `MotionImpulse` is never pushed.
- The shake is a `camera.shake` profile at `per_unit` times the change, capped
  at `max`, for the view the recipient owns alone (`docs/gameplay/camera.md`).
- The animation asks the recipient's rig for an intent, as a Fixed request of
  `ticks` from the authority, so it replicates like any other. `front`, `back`,
  `left` and `right` name intents for the side the blow came from: the strike's
  direction read against the way the recipient faces. Only `front` is required,
  and it answers any side that names none and a blow with no direction. A rig
  plays one on a layer whose selector reads it; an additive layer over the
  upper body lets a character flinch while it walks.

`ReactToEffect` is the decision as a pure function of the rules and one
application, so a game can test its own profiles without a World.

## Limits

- Reactions run on the authority. A remote client's own shake, and how a
  pushed predicted pawn is corrected, are in `docs/deferred.md`.
- A profile names attributes and tags; names its World does not know are
  logged once, and a reaction naming an unknown attribute never matches.
