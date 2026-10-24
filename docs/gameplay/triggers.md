# Triggers

A trigger is a placed entity whose sensor runs an authored sequence: when
something walks into a volume, when it stays or leaves, or when it asks to
interact, the volume's commands run, in order, through the same verbs every
other producer uses. A bumper, a kill floor, a door that opens on approach and
a one-shot story beat are the same thing with different data.

Three mechanisms, each with one job:

| Mechanism | Owns | Knows nothing of |
|---|---|---|
| `SensorOverlaps` (`docs/physics/sensor-overlaps.md`) | which bodies and characters overlap which sensors, sample by sample | gameplay, cooldowns, commands |
| `Trigger` + `TriggerSystem` (`logic/Trigger.h`, `logic/TriggerSystem.h`) | when an overlap activates a trigger: mode, enabled, once, cooldown, stay cadence, authority, budget | what the commands do |
| `VerbSequence` (`logic/VerbSequence*.h`) | what runs: conditions, then verbs in order, with their inputs | sensors, volumes, activation policy |

Relays (`logic/VerbRelay.h`) and triggers share everything below the
response: each command is an ordinary compiled verb binding, dispatched by the
same `VerbDispatcher` with the same schema checks, revisions, provenance and
trace, at the same fixed-logic boundary under the same liveness and partition
rules. They differ in what they hold. A relay names one binding in a shared
asset and is activated by native code with typed inputs; a trigger holds its
own ordered, conditional response inline, where a designer edits it on the
volume. Routing a trigger through a relay's queue would put a second queue and
an ordering edge between them without removing either check.

The sequence is not trigger-specific: it reads named *ports* its producer
offers, and a trigger offers two. A later producer of authored responses --
a timer, a LevelFlow node -- offers its own ports and runs the same sequences
through the same store, compiler and runner.

## Authoring in Kyusu

1. Draw a box with the Brush tool.
2. Select it and choose **Make Trigger Volume** (the context wheel or the
   palette). One undoable step gives it `volume`, `volume_sensor` and
   `trigger`: it is now air, it cooks to a sensor, and it activates. It draws
   orange, with TRIGGER over it.
3. In the inspector, the Trigger group's first rows are its policy; below them:
   - **Only when** -- conditions, all of which must hold. *Add Condition* offers
     "Entity has…" (an entity port carries components and tags) and every query
     that answers true or false.
   - **Then** -- commands, run in order. *Add Command* offers every instant verb
     the project declares. Each argument comes from a constant, a port
     (`activator`, `trigger`) or an entity by its persistent identity; a
     direction can be given in the trigger's frame, so it turns with the volume.
     Each command says whether a failure continues to the next or stops.
   - **Problems** -- anything the sequence does not resolve against the
     project's vocabulary, and a volume that would sense nothing.
4. Resize and move it with the ordinary brush tools. Save, cook, play.

Every change is one undoable step, and the sequence saves exactly as written --
a verb a module no longer declares is reported, not dropped.

### The bumper

The FPS template's `levels/bumper.sscene` is the worked example:

```json
"trigger": {
  "activation": "enter", "cooldown": 0.5, "cooldown_scope": "activator",
  "sequence": {
    "conditions": [{ "entity": "activator", "has": ["sencha.character_movement"] }],
    "commands": [{ "verb": "movement.launch", "arguments": {
      "target": { "port": "activator" },
      "direction": { "const": [0, 0, 1], "frame": "trigger" },
      "speed": 8, "rise": 9 } }]
  }
}
```

Its arrow in the view points along the launch. Swapping `movement.launch` for
any other verb -- `effect.apply`, `trigger.disable`, a game's own -- changes
what the bumper does without touching trigger code.

### Cooking

A volume marked `volume_sensor` reaches the cooked level as its own entity --
persistent identity, transform, hierarchy and runtime components kept, editor
components stripped -- carrying a `collider` that is a box sensor fitted
exactly to the brush (`document/VolumeSensorCook.h`). The box is posed in the
entity's body frame (its position and turn); its scale is baked into the box.
A volume that is not exactly one box -- a cylinder, a dragged corner, modifiers
that make several pieces, a non-uniform scale across a turn -- refuses the cook
with its name and the reason. It is never approximated. A hidden trigger volume
is left out of the cook, as a hidden brush is. Volumes are never geometry, so
none of this draws, collides or blocks navigation.

A trigger need not be a brush: any entity with a `Trigger` and a sensor
collider (`Collider.IsTrigger`) works, as a prefab might carry one.

## Runtime semantics

### Activation

The physics phase publishes a sample after characters move. `TriggerSystem`
reads it at the next fixed-logic pass, before the movement producers, so a
launch lands on the very next movement step.

| Activation | Runs on |
|---|---|
| `enter` | an activator's first sample inside, a fast linear crossing included |
| `stay` | entry, then every `stay_interval` seconds while inside (zero: every tick) |
| `exit` | leaving, a fast crossing included |
| `interaction` | `trigger.interact {activator}` while the activator is inside |

Several activators in one sample run in complete-identity order, and a
sequence's commands in authored order. A disabled trigger ignores samples;
re-enabling it does not run for someone already inside (entry is an edge).

`cooldown` (seconds) starts when a sequence ran -- its conditions held -- and
applies to the whole trigger or to each activator (`cooldown_scope`). `once`
ends the trigger entity after its first run; a second activator in the same
sample does not run it.

### Sequences

Conditions are checked first, against the ports as they are now. An entity
condition needs every listed component and the tag query to match; a query
condition needs the answer it expects. A condition that did not compile never
holds.

A command is a compiled verb binding (`authored/VerbBindingCompiler.h`): plain
constants compile into it, and every other argument becomes a binding input the
runner fills -- a port's value, a query's answer or one member of it, turned
from an entity's frame into the world, or scaled. Shapes are checked when the
sequence compiles, against the verb's or query's declared arguments, and the
dispatcher checks values again when it runs, as for every producer. Each
invocation carries the trigger as producer, the activator as instigator and
the fixed tick. Only instant verbs run from a sequence; a lease has no holder
to release it.

A command fails when the dispatcher does not accept it, when one of its values
cannot be read, or when it never compiled. `on_failure: "stop"` ends the run
there; the default continues. Outcomes are counted on `TriggerSystem::Stats()`.

Compiling happens once per distinct sequence and again only when a verb or
query catalog publishes or retires something, the tag vocabulary grows, or a
data asset the sequence names arrives or reloads.

Components cannot own variable data, so a trigger holds a `VerbSequenceKey`:
the hash of its sequence's authored text, in the World's store. A key is a
value. The store keeps every entry it interned for the World's lifetime, so a
component copied, snapshotted for undo, or restored from a prefab baseline byte
for byte always resolves, and entities authored alike share one entry. The
store grows with the distinct texts a World has seen -- in Kyusu, one per
distinct text an edit produces. Saving a key the World never interned (bytes
carried in from another World) fails loudly rather than writing an empty
sequence.

### Bounds

Activations wait in a bounded queue (`logic.trigger.queue_capacity`); a full
queue refuses and counts. At most `logic.trigger.command_budget` commands run
a tick; activations past it wait, in order, for the next tick, and are checked
again when they run: both entities alive and in the tick's logic partitions,
the trigger still a trigger and enabled, its cooldown elapsed. A zone that
detaches takes its waiting activations and cooldowns with it; a zone that
returns starts with fresh Enter samples.

`trigger.interact` admits a request answered at the next pass, never inside
the pass that issued it, so a trigger whose command interacts with another
cannot chain within a tick. Verbs that act immediately (`trigger.enable`) do so
as any verb does; the dispatcher refuses re-entry.

A tick with no sensor sample costs a resource lookup. The sensor contract's
overflow -- a whole sample rejected -- is counted on `Stats().SensorOverflows`
and logged: crossings in it are lost, which is never evidence a volume was
empty.

### Authority and the network

Only the simulation authority runs a sequence -- standalone, a listen server's
host, a dedicated server. A client neither runs nor remembers activations: it
sees their effects through replication. `movement.launch`, `trigger.enable`,
`trigger.disable` and `trigger.interact` are authority-only and refuse on a
client as well.

A launched pawn's client learns of the launch from the authority's state: its
prediction is corrected by the usual reconciliation, so the launch reads a
round trip late on the client that owns the pawn. Nothing predicts it.

### What persists

Authored policy and the sequence are scene data. Cooldowns, stay timers and
waiting activations are transient: a zone that streams out and back comes back
armed as authored. A consumed `once` trigger is durable: its entity ends, which
the zone state overlay (`zone/ZoneStateStore.h`) remembers across unload and
reload, and which no late join can undo. `trigger.enable`/`disable` change a
field, which streaming does not remember today (`docs/deferred.md`).

## The vocabulary

| Name | Kind | Does |
|---|---|---|
| `trigger.enable {trigger}` | verb, authority only | lets a trigger activate again |
| `trigger.disable {trigger}` | verb, authority only | stops it until enabled |
| `trigger.interact {activator}` | verb, authority only | activates every Interaction trigger the activator stands in, at the next pass |
| `movement.launch {target, direction, speed = 10, rise = 0}` | verb, authority only | `docs/gameplay/movement.md`, "Launch" |

## Diagnostics

The inspector lists a sequence's problems beside it. At runtime, a sequence's
diagnostics are logged once each time it compiles, and `TriggerSystem::Stats()`
counts activations, gated activations (disabled, cooling, conditions), invoked
and failed commands, refused admissions, abandoned activations and sensor
overflows.

## Inspector keys

`VerbSequenceRows` (`editor/common/src/vocabulary/VerbSequenceRows.h`) keys its
rows so an edit names what it changes: `conditions` and `commands` are the
lists, `c<i>` and `k<i>` their entries; a command has `k<i>.verb`,
`k<i>.on_failure` and `k<i>.arg.<name>.source|value|frame`; a condition has
`c<i>.entity`, `c<i>.has`, `c<i>.tags.all|any|none`, `c<i>.query`,
`c<i>.expect` and `c<i>.arg.<name>.source|value`. A value typed for an argument
nothing sets yet sets it as a constant.
