# Responses

An entity's responses are the authored answers it gives to occurrences
delivered at it: something entered its volume, a shot struck it, someone used
it, it detonated. Each answer is a binding -- which declared event it answers,
its policy, and a sequence of conditions and commands run through the same verbs
every other producer uses. A bumper, a kill floor, a door that opens when shot
with the right weapon and a switch that answers three different things are the
same thing with different data.

| Mechanism | Owns | Knows nothing of |
|---|---|---|
| `AuthoredEventRegistry` (`authored/AuthoredEventRegistry.h`) | which occurrences exist and their typed payloads, declared by the engine and by games | delivery |
| Producers -- `SensorOverlaps` and any native system | noticing an occurrence and delivering it at the entity it happened to | what anyone does about it |
| `Responses` + `ResponsesSystem` (`logic/Responses.h`, `logic/ResponsesSystem.h`) | whether that entity answers it, per-binding policy and state, the queue, authority, budget, liveness | detection; cross-entity orchestration |
| `VerbSequence` (`logic/VerbSequence*.h`) | what runs: conditions, then verbs in order, reading named ports | activation policy |
| `AuthoredEventDispatcher` | global fan-out to subscribers | responses |

**Local and global are separate operations over one vocabulary.**
`ResponsesSystem::Deliver(subject, occurrence)` evaluates `subject`'s own
bindings and nothing else; it never publishes. `AuthoredEventDispatcher::Publish(
subject, occurrence)` reaches global subscribers and never runs responses. Both
take the same declared event and the same typed struct; a producer that means
both calls both. A binding hears only occurrences at its own entity -- a rule
about another entity's occurrence is orchestration, which subscribes globally.

Native mechanics stay native. Damage is an effect, depletion is the owning
component's system, motion is movement's. A projectile applies its damage and,
separately, delivers an occurrence for whatever authored behavior the struck
entity has; an entity without `Responses` costs that delivery a validity check
and one archetype bit.

## Declaring an occurrence

A game declares its events in its vocabulary hook, as it declares verbs:

```cpp
AuthoredEventDefinition impact;
impact.Name = "lf.ctx.impact";
impact.DisplayName = "Shot by CTX";
impact.Payload.Children = { /* instigator: Entity, attack: GameplayTag, point, normal: Vector 3 */ };
impact.Instigator = "instigator";   // the payload field that is who is responsible
(void)events.Declare(std::move(impact));
```

The struct a producer delivers names the event and encodes its payload, as for
`Publish` (`AuthoredApiDefinition<E>::EventName` and `Encode`). Declare one event
per kind of occurrence -- a different relationship to the subject or a
different payload shape -- not one per weapon or variant: which attack made an
impact is a tag field the bindings' conditions read.

The engine declares the sensor events: `sensor.enter`, `sensor.stay` (every
sample inside, entry included), `sensor.exit` and `sensor.interact`, each
carrying its `activator` as instigator.

## Authoring in Kyusu

One component, two presentations, chosen by what the entity is:

- **Trigger** -- a volume brush, or anything with a sensor collider. Draw a box
  with the Brush tool, then **Make Trigger Volume** (the context wheel or the
  palette): one undoable step gives it `volume`, `volume_sensor` and
  `responses`. It draws orange with TRIGGER over it, and an arrow for every
  direction its commands are given. A new one offers an **On Enter** response to
  start from; it is written only once something in it is edited.
- **Behaviors** -- anything else: the same component headed Behaviors, with no
  viewport marks. **Add Behavior** offers every declared event except the
  sensor events.

Each binding is a card: what it **Answers**, **Starts enabled**, **Once**,
**Cooldown** and what the cooldown applies to (the binding, or each
instigator), then **Only when** -- conditions, all of which must hold -- and
**Then** -- commands in order. *Add Condition* offers "Entity has…", "Tag is…"
where the event carries a tag, and every true-or-false query. Each argument
comes from a constant, a port, or an entity by its persistent identity; new
commands start aimed at the event's instigator, as a bumper launches whoever
entered it. **Problems** lists what does not resolve against the project's
vocabulary, and a volume that would sense nothing.

Every change is one undoable step, and bindings save exactly as written -- an
event or verb a module no longer declares is reported, not dropped.

### The scene form

```json
"responses": { "bindings": {
  "launch": {
    "on": "sensor.enter", "cooldown": 0.5, "cooldown_scope": "instigator",
    "sequence": {
      "conditions": [{ "entity": "activator", "has": ["sencha.character_movement"] }],
      "commands": [{ "verb": "movement.launch", "arguments": {
        "target": { "port": "activator" },
        "direction": { "const": [0, 0, 1], "frame": "self" },
        "speed": 8, "rise": 9 } }] } } } }
```

That is the FPS template's bumper (`levels/bumper.sscene`). Bindings are an
object keyed by stable keys (letters, digits, `_`, `-`): the key is a binding's
identity, so a prefab instance overrides one binding by patching
`bindings.<key>` and leaves the others as inherited. A binding's members:
`on` (required), `enabled` (default true), `once` (default false), `cooldown`
in seconds, `cooldown_scope` (`"binding"` or `"instigator"`), and `sequence`.
At most 32 bindings; more are refused with the entity's name.

### Ports

A binding's sequence reads `self` -- the entity it belongs to -- then one port
per field of its event's payload, by the field's name and kind: an entity, a
vector, a gameplay tag, a number. A tag condition, `{"tag": "<port>", "tags":
{"all", "any", "none"}}`, matches a tag port hierarchically, so an attack
tagged `lf.ctx.attack.charge.strong` satisfies `any: [lf.ctx.attack.charge]`.

## Runtime semantics

### Delivery and admission

`Deliver` checks, in order: a valid subject; the `Responses` bit in its
archetype (a cached component id, no hash); the authority; then the subject's
compiled bindings by pre-resolved event id. Only when a binding answers the
event is the payload encoded -- once per delivery, into a reused pool, and
checked against the declaration. A binding admits the occurrence unless it is
disabled, consumed, or cooling down; admitted, it captures what its run needs:
the recipient and its zone partition, the binding's set and sequence keys and
policy, the event's contract revision, the delivery tick and the payload.

### When responses run

| Occurrence | Runs at |
|---|---|
| delivered during tick N, whichever side of the response pass its producer runs | the response pass of tick N+1 |
| delivered by a command running in a response | the next tick's pass: responses never run inside responses |
| a sensor sample taken at the end of tick N | the pass of tick N+1, before movement, as the pass observes it first -- a launch lands on that tick's movement step |
| `trigger.interact` asked during tick N | the pass of tick N+1 |
| published globally and delivered by a subscriber during the drain | the next tick's pass |

Within a pass, responses run first-in first-out, each sequence's commands in
authored order. An occurrence that several bindings answer is one entry per
binding, in key order. At most `logic.responses.command_budget` commands run a
pass; what the budget does not reach waits, in order. A full queue
(`logic.responses.queue_capacity`) refuses at `Deliver`, which says how many
bindings admitted; the refusal is counted and logged once.

### Policy and state

Each binding keeps its own state; nothing a policy does creates or ends an
entity.

| State | Meaning |
|---|---|
| eligible | admits occurrences |
| disabled | admits nothing until `responses.enable`; authored or set by `responses.disable` |
| cooling | admits nothing from the binding's cooldown, or from that instigator's |
| consumed | a once-binding whose conditions held; admits nothing again |

A response is gated twice: at admission, and again when it runs, against the
state then. So two shots that reach a once-switch in one tick open it once,
and a cooldown that began after an occurrence was admitted still holds it back.
The first admitted occurrence whose conditions hold spends a once-binding --
a default shot and a charge shot in one tick still open a charge-only switch.
Conditions that fail change nothing; commands that fail do not undo the run.
Cooldowns start when conditions held.

### Lifetimes

| At the pass | The captured response |
|---|---|
| recipient alive, its zone in logic | re-gated on live state, run, state recorded |
| recipient destroyed since | runs what it captured; records nothing, there being nothing to record on |
| recipient's zone detached or dormant | abandoned |
| the event's declaration changed since | abandoned: the payload was encoded against the old one |
| the entity's bindings replaced since | abandoned |

So a barrel that delivers its detonation and is destroyed the same tick still
runs its response; commands naming `self` meet a stale generational id, which
every verb checks, and what the response needs from the barrel belongs in the
payload. An instigator or other payload entity that ended is likewise a stale
id: a condition on it fails, which is what keeps a bumper from launching what
is gone.

### Authority and the network

Only the simulation authority admits or runs a response. A client neither
admits nor remembers anything; it sees what responses changed through
replication. `responses.enable`, `responses.disable` and `trigger.interact` are
authority-only and refuse on a client.

### What persists

Bindings are scene data. Runtime state -- disabled and consumed bits -- is
remembered by the zone: when a zone detaches, each entity whose bindings differ
from authored is recorded in `ZoneStateStore` by persistent identity and
binding key, and restored when the zone attaches again. A binding is restored
only if its authored text is unchanged; changing a binding's text starts it as
authored. Cooldowns and waiting responses are transient. The same rule applies
when an entity's bindings change at runtime or in Kyusu: state follows the
key, and only while the binding's text is unchanged.

### Hot reload

Compiled bindings are checked once per pass against the verb, query and event
catalogs and the tag vocabulary, and recompile when any moved; a data asset a
command names is checked by its reload stamp. An event retired or re-declared
with a different payload is reported against the bindings that name it, and
responses captured under the old declaration are abandoned.

## The vocabulary

| Name | Kind | Does |
|---|---|---|
| `responses.enable {entity, binding = ""}` | verb, authority only | lets a binding -- or all of them -- respond again |
| `responses.disable {entity, binding = ""}` | verb, authority only | stops it until enabled |
| `trigger.interact {activator}` | verb, authority only | delivers `sensor.interact` at every volume the activator stands in, at the next pass |
| `sensor.enter`, `sensor.stay`, `sensor.exit`, `sensor.interact {activator}` | events | a sensor volume's occurrences |
| `movement.launch {target, direction, speed = 10, rise = 0}` | verb, authority only | `docs/gameplay/movement.md`, "Launch" |

## Diagnostics

Kyusu lists a binding set's problems beside it. At runtime, a sequence's
diagnostics are logged once each time it compiles, and
`ResponsesSystem::Stats()` counts deliveries that reached an entity with
responses, admissions, encodes, gated and run responses, invoked and failed
commands, refused admissions, abandoned responses, refused payloads and sensor
overflows. `test/runtime/ResponsesDeliveryBench.cpp` measures delivery and pass
costs and asserts that warmed paths do not allocate.

## Inspector keys

`ResponsesEditorAdapter` keys each binding's rows `b.<key>.`: `on`, `enabled`,
`once`, `cooldown`, `cooldown_scope`, and `s.` followed by the sequence's own
keys. `VerbSequenceRows` (`editor/common/src/vocabulary/VerbSequenceRows.h`)
keys a sequence's rows so an edit names what it changes: `conditions` and
`commands` are the lists, `c<i>` and `k<i>` their entries; a command has
`k<i>.verb`, `k<i>.on_failure` and `k<i>.arg.<name>.source|value|frame`; a
condition has `c<i>.entity`, `c<i>.tag`, `c<i>.has`, `c<i>.tags.all|any|none`,
`c<i>.query`, `c<i>.expect` and `c<i>.arg.<name>.source|value`. A value typed
for an argument nothing sets yet sets it as a constant.
