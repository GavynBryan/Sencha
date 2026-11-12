# Decision agents

A decision agent is an entity that chooses its own options. On its own clock it
picks a target from what it remembers, asks its profile's selection table which
option to take, chooses a place for that option if it needs one, and requests
the option's ability. It moves, aims and attacks through those abilities, never
by itself.

It joins three pieces that already stand alone: perception memory
(`docs/spatial/perception.md`), selection tables (`selection.md`) and candidate
evaluation (`docs/spatial/candidates.md`).

Code: `engine/include/logic/decision/`, `engine/src/logic/decision/`.
Tests: `test/runtime/DecisionAgentTests.cpp`.

## What it owns and what it does not

The engine owns:

- **Scheduling.** Each agent decides every `decision_seconds`, staggered by
  entity so a crowd does not decide on one tick.
- **Targets.** The target is the nearest remembered subject holding the
  profile's `hostile` tag. Once it has had a target, the agent keeps what it
  last knew after memory lets go.
- **Binding the table.** Inputs are bound by the names the game registers.
- **Running an option.** This covers claims within the agent's group, ruling
  out an option that has no ability or whose claim is full, and cancelling the
  old ability and activating the new one.
- **Choosing a place.** Slots are filled by registered providers, a stale place
  is chosen again, and a group remembers the places its members have visited.
- **The record.** The last decision is kept for `Describe`, debug drawing and
  a cost figure.

The game owns:

- the system that calls `DecisionAgents::Run`, and when it is scheduled;
- every input a table reads, registered by name;
- what it adds through hooks: its own clocks, squad state it gathers before a
  decision, and what an option means to it beyond its ability;
- death, aim and presentation.

There is no behaviour tree. A table re-weighs every option each decision, and a
plan's `commits` holds an option until its ability ends.

## The component

`DecisionAgent` holds what the loop tracks: its clock, target, chosen option,
running ability, destination, ruled-out options, last contact and claim. It is
the authority's, runtime-only, and never saved. A game usually provisions it
from its own component with `DerivedComponents`, so it arrives with the entity
at spawn.

## The profile

`DecisionProfile` is plain data that a game embeds in its own profile asset.
`ReadDecisionProfile` reads it, and `MakeDecisionProfileFields` gives the
schema, letting the game add members to each option's record.

```json
{
  "hostile": "faction.player",
  "table": "asset://data/combat/soldier.table.sdata",
  "decision_seconds": 0.25,
  "unavailable_seconds": 1.0,
  "options": [
    { "option": "flank", "ability": "asset://abilities/move.sdata",
      "position": "asset://data/combat/flank.sdata",
      "reposition_distance": 4, "position_seconds": 8, "replace_on_arrival": false,
      "claim": { "tag": "claim.flank", "count": 1, "share": 0.25 } },
    { "option": "search", "ability": "asset://abilities/move.sdata",
      "position": "asset://data/combat/search.sdata", "marks_visited": true },
    { "option": "chase", "ability": "asset://abilities/move.sdata", "to_last_seen": true },
    { "option": "reel", "ability": "asset://abilities/reel.sdata", "commits": true }
  ]
}
```

A place is chosen again when one of these happens:

- it is older than `position_seconds`;
- the target has moved more than `reposition_distance` from where it was when
  the place was chosen;
- with `replace_on_arrival`, the agent has reached it.

An option whose evaluation finds nowhere is ruled out for `unavailable_seconds`.

## Slots

The engine fills these slots of a place evaluation by name:

| Slot | Holds | Bound when |
|---|---|---|
| `target` | the target as seen, or where it was known to be and its facing | it has or had a target |
| `target_position` | where the target is known to be | it has or had a target |
| `current` | its current destination | it has one |
| `contact` | the last contact carried on along its motion, up to two seconds | it had a target |
| `self` | where the agent stands | always |
| `predicted` | the target walked on for 1.2 seconds, stopped at walls and ledges | it had a target |
| `visited` | places its group reached in the last 30 seconds | always |

A game registers more with `RegisterSlot`, for example its squad's members. It
may also replace a built-in slot by registering the same name.

## Hooks

`DecisionHooks`, all optional:

| Hook | Called |
|---|---|
| `ProfileOf` | for the profile an agent decides by |
| `Bound` | when a profile's table binds, or binds again |
| `Turn` | every tick before its clocks run; returns Decide, Hold (clocks run, no decision) or Skip (left alone) |
| `TargetFound` | when it has a target again after having none |
| `Prepare` | once the target is known, before anything is measured |
| `OptionEnded` | when the running option's ability has ended |
| `OptionChosen` | when it chose an option, or a new place for the one it holds |
| `Stopped` | when nothing qualified |

## Determinism and authority

It runs on the simulation authority only. Due agents decide in entity order.
Table and place runs are seeded from the agent and its decision count. Place
runs share a budget per tick (`DecisionRunSettings::PlaceRuns`); an agent over
the budget keeps its option and asks again on the next tick.
