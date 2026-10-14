# Input mapping

Status: current architecture (2026-08).

Gameplay reads *actions* — `move`, `jump`, `look` — and never learns which key or
button produced one. Controls are authored in data, so rebinding a game is an
asset edit rather than a recompile, and a menu can take a key away from gameplay
without either side knowing about the other.

## The path an input takes

```
SDL events
  └─ SdlInputCapture              → InputFrame          raw device state, per rendered frame
       └─ InputActionResolveSystem                      the only gameplay-path reader of InputFrame
            ├─ PreSimulate  (1× per frame) → InputActionState::Frame()
            └─ FixedLogic   (1× per tick)  → InputActionState::Tick()
                 └─ game systems → MovementIntent, AbilityActivationQueue
```

`InputFrame` (`input/InputFrame.h`) stays what it was: a platform-agnostic
snapshot of held keys, button state, edges, and pointer motion. It remains the
right thing to read in the editor, in debug tooling, and in a future rebinding
screen that needs to know which physical control the player just pressed.

It is not reachable from simulation. Only `PreSimulateContext`, where the mapper
runs, and `FrameUpdateContext`, where the tooling lives, carry it; the fixed-tick
and later contexts do not. The mapper captures a device snapshot once per frame,
so every tick of that frame resolves against identical device state.

Everything above it speaks actions.

## Authoring

Two `.sdata` subtypes, because they vary independently. Actions are declared once
per game; a game ships several profiles over them (keyboard, gamepad, a player's
saved rebinds).

`input_actions.sdata` — the vocabulary:

```json
{ "type": "input.actions", "version": 1, "data": { "actions": [
    { "name": "move", "type": "axis2" },
    { "name": "look", "type": "axis2" },
    { "name": "jump", "type": "digital", "fire": "held" },
    { "name": "pause", "type": "digital", "scope": "presentation" }
]}}
```

`type` is `digital`, `axis1`, or `axis2`. `scope` is `simulation` (the default —
the action drives the simulation and may travel in a player command) or
`presentation` (local to this client: menus, debug toggles).

`fire` says which moment counts as the action firing: `pressed` (the default —
once, as the control goes down), `released` (once, as it comes up), or `held`
(every pass it is down, which repeats). It belongs to the action rather than to
the code that reads it, so why holding jump jumps repeatedly is answerable from
the input mapping instead of from C++, and changing it is an edit to data. Only
a button has a moment to fire on; authoring `fire` on an axis action fails the
asset rather than becoming a knob that silently never applies.

`input_default.sdata` — controls bound to those actions, grouped into contexts:

```json
{ "type": "input.profile", "version": 1, "data": {
    "actions": "asset://data/input_actions.sdata",
    "contexts": [
        { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "move", "composite": "cardinal",
              "left": "key.a", "right": "key.d", "down": "key.s", "up": "key.w" },
            { "action": "look", "control": "mouse.delta", "scale": 0.0025 },
            { "action": "jump", "control": "key.space" }
        ]}
    ]}}
```

A binding names either one `control` or a `composite` of buttons (`axis` for a
signed scalar, `cardinal` for a plane in `left`/`right`/`down`/`up` order).
Optional conditioning: `scale`, `dead_zone`, `invert_x`, `invert_y`, and
`normalize` (keeps a cardinal composite inside the unit circle so diagonals are
not faster). Several bindings may drive one action; digital contributions or
together, axis contributions sum.

Control names are `key.<name>`, `mouse.left|right|middle|x1|x2`, `mouse.delta`,
`mouse.wheel`, and the gamepad set below. Key names come from the platform's
scancode table with spaces written as underscores (`key.left_shift`), so a
binding names a physical key position rather than a layout-dependent letter.

Gamepad controls are named by position on the pad, not by the glyph printed
there, so one binding covers pads whose face buttons carry different letters:
`gamepad.south|east|west|north`, `gamepad.back|guide|start`,
`gamepad.left_shoulder|right_shoulder`, `gamepad.left_stick_click`,
`gamepad.right_stick_click`, `gamepad.dpad_up|dpad_down|dpad_left|dpad_right`,
`gamepad.left_trigger|right_trigger` (scalars), and
`gamepad.left_stick|right_stick` (planes). SDL's mapping database normalizes
layouts, so an Xbox, PlayStation, or generic pad all arrive as the same set.

Sticks report negative Y when pushed away from the player, matching the mouse's
downward-positive convention. A movement binding therefore wants `invert_y`,
while a look binding does not — the template profile shows both.

Every connected pad drives one abstract pad. `GamepadDeviceSet` keeps what each
device is reporting and folds them: a button reads down while any device holds
it, and each stick or trigger takes the device pushing it furthest. One player
with a controller in each hand is still one player, and a pad unplugged
mid-press stops contributing rather than holding its buttons down forever.
Per-player device sets are a split-screen concern and are not this.

Sticks and triggers are *positions*, not displacement: they hold their value
until the device moves, so every tick of a catch-up frame reads the same stick.
Only mouse motion and the wheel accumulate.

A trigger can drive a button action, which is how fire and jump are normally
bound on a pad: `threshold` says how far it must be pulled to count as pressed,
defaulting to half travel. The crossing is what produces the press and release
the action's `fire` mode then selects over,
and it is tracked whether or not the binding's context is active -- so switching
a context on over an already-pulled trigger reads as held, never as a press the
player did not make. The wheel cannot drive a button action: it reports how far
it moved rather than how far it is held, so there is no position to compare.

Both subtypes are runtime formats: no cook step, and the existing `.sdata`
watcher hot-reloads them in place while the game runs.

## Authoring them

Both subtypes have purpose-built surfaces in the Data Editor, so the JSON above
is what gets written rather than what gets typed.

A profile presents as the keymap it describes: a collapsed context reads
"gameplay - priority 100 - 5 bindings", a collapsed binding reads
"jump - Space" or "move - WASD". Where something under a card is wrong, the card
carries a marker, so a mistake cannot hide behind a collapsed row.

An action card names the shape it carries and any moment that is not the
default, so "jump - Button, while held" is legible from the collapsed row. The
fire mode is offered only on a button, and turning a button into an axis takes
the mode with it rather than leaving an unauthorable field behind.

A binding's shape is chosen once -- single control, two buttons, or four
directions -- and choosing it erases the slots the other shapes use. That makes
the compiler's "exactly one of control or composite" rule unauthorable to
violate, and means only the slots that apply are ever on screen.

Control slots offer three ways to fill them, in increasing order of how much the
author has to know: **Listen** binds whatever control they press; **Pick** lists
what this platform can bind, grouped by device with a filter and the authored
name beside each label; and the text stays editable for anyone who already knows
the name. A name that parses to nothing says so at the field.

The action field lists what the referenced action set declares, with
shape-incompatible actions disabled and the reason on hover. The set is read
from its open tab when it has one, so an action added moments ago is bindable
before it is saved. An action the set does not declare is flagged but never
cleared -- it may be one the author is about to add -- and actions declared but
bound nowhere are listed at the bottom of the profile.

These surfaces are ordinary subtype editors (`editor/data_editor/src/input/`),
registered in `SubtypeEditors.cpp` like any other. A subtype with no editor
keeps the schema-generated form.

## Reading actions

```cpp
// Resolved once at startup: names to dense ids.
struct FpsInputActions { InputActionId Move, Look, Jump; };

void FpsSteeringSystem::FixedLogic(FixedLogicContext& ctx)
{
    const auto* ids = ctx.Entities.TryGetResource<FpsInputActions>();
    const auto* actions = ctx.Entities.TryGetResource<InputActionState>();
    if (ids == nullptr || actions == nullptr)
        return;

    const InputActionView input = actions->Tick();
    const Vec2d move = input.Axis2(ids->Move);   // strafe on X, forward on Y
    const bool jump = input.Fired(ids->Jump);    // the moment the action set authored
    ...
}
```

`Fired()` is what single-phase gameplay reads: it answers under the action's own
`fire` mode, so a designer retuning a button does not need the consumer to
change. Gameplay built out of several moments — charge while held, loose on
release, one shot per press of a semi-automatic weapon — walks `Edges` in order;
`Held()`, `Pressed()`, `Released()` and `Interrupted()` summarize them.

`Tick()` is this tick's record; `Frame()` is the presentation snapshot, which is
what a camera or a menu wants. Which clock a reader is on also decides how it is
ordered against the resolve system, because `EngineSchedule::After` is
phase-local: it records an edge only in phase lists both systems appear in, and
an edge between systems sharing no phase orders nothing and asserts.

A `FixedLogic` or `PreSimulate` reader shares a phase with the resolve system,
so it declares the edge:

```cpp
ctx.Schedule.After<FpsSteeringSystem, InputActionResolveSystem>();
```

A `FrameUpdate` reader must not. `InputActionResolveSystem::PreSimulate` runs in
`FramePhase::ScheduleTicks` and `FrameUpdate` systems run in
`FramePhase::Update`, so every frame resolves input before presentation-rate
systems read it. The ordering is structural; declaring it as an edge only trips
the assert. Note this is a reason to keep a camera or menu system on
`FrameUpdate` rather than moving it to satisfy an edge -- reading the
presentation snapshot at tick rate is the bug the phase split exists to prevent.

Adding an action costs one entry in the action set, one binding in the profile,
and one field wherever it is consumed. No engine edit, no central switch.

## The shell's own actions

`ui.back`, `ui.accept` and the four directions belong to the engine. It declares
them and their default bindings, and merges them into whatever profile a game
binds -- including into no profile at all, so a game shipping no input content
still answers the key that opens its menu.

`ui.` is reserved: an action set may redeclare one of these and only these, with
a matching type and scope, which is how a game rebinds one. Any other `ui.` name
fails the asset. A redeclaration keeps the engine's id, replaces the default
binding rather than adding to it, and is compiled into the engine's own `ui`
context whichever authored context declared it -- because authored contexts stop
resolving while the application shell has input suspended, and a Back that went
silent under a pause could never lift it.

The `ui` context sits above any authored priority, and an authored context at or
above that band is refused. See [pause.md](pause.md).

## Contexts

A context is a named group of bindings that can be turned on and off at runtime.
Activation returns a lease; the context stays live until the lease is dropped, so
a menu that is torn down or a dialogue interrupted by a load cannot leave its
context stuck on. Leases are counted, so two holders do not cancel each other.

```cpp
InputContextLease menu = world.GetResource<InputContextSet>().Activate("menu");
// ... dropping `menu` deactivates it
```

`InputContextSet::SetSuspended` is the other switch, and it belongs to the
application shell: every context except the engine's own goes quiet while a menu
owns input, and every lease stays exactly where it was. It suppresses rather than
deactivates because the leases belong to whoever took them -- a game gets its
controls back untouched without having to notice the shell existed. Priority
could not do this: a context claims only the controls it *binds*, so a menu
context above gameplay would leave movement and firing resolving.

Every context has a unique priority within its profile. When several active
contexts bind the same control, the highest-priority one claims it and the
control reads as absent to everything below — per control, not per context, so a
menu that binds Escape does not also take movement. To block a whole context,
deactivate it; there is no separate "consume everything" flag.

## Frame and tick semantics

A rendered frame may run zero, one, or several fixed ticks against one sample of
input. Once per frame, after contexts settle, the mapper turns the frame's button
transitions into **action edges** and hands the same edges to both clocks; each
clock keeps what it has not consumed, along with its share of pointer motion.

### Edges

The platform layer logs every button transition in the order it arrived
(`InputFrame::TransitionLog`, 64 per frame). The mapper replays that log under the
frame's contexts and claims and records, in order, each time an action rises or
falls:

| Edge | When |
|---|---|
| `Press` | a transition the player made raised the action |
| `Release` | a transition the player made lowered it, or a trigger came back under its threshold |
| `InterruptedRelease` | the action fell for any other reason: a context left or was suspended, a higher context claimed the control, the profile was lost, or focus was (the platform's synthetic releases) |

Every pass carries its edges as an ordered list across all actions
(`InputActionView::Edges`, at most 32), so a re-press while held, two taps in one
frame, and which of two actions went first all survive. A rise nobody pressed for
— a context returning over a key still down — holds the action silently; the
player's next key-up is then a `Release` with no `Press` before it. A genuine
key-up in the same frame a context leaves is attributed after the context change,
so it reads as interrupted: ambiguity resolves toward cancelling.

The per-action flags summarize the list: `Pressed`, `Released` (any fall),
`Interrupted` (an interrupted fall), `Held` (the action's state now), and `Fired`
under the action's mode. A `Released`-mode action fires only on a genuine
`Release`, never on an interruption, so a charge does not loose because a menu
opened.

If more edges arrive than a pass can carry, the rest are dropped and the list is
marked `Overflowed`. The summary stays true regardless: a pass whose kept edges end
held while the action is really up publishes an interrupted fall, and one whose
kept edges end up while the action is down holds it with no press. An overflow
therefore never invents a press or a release the player made, and never leaves an
action held that is not.

| Situation | Behaviour |
|---|---|
| Zero ticks | The presentation clock still resolves. The simulation clock keeps the edges and motion until a tick runs, so nothing is dropped. |
| One tick | The tick takes every edge since the previous tick. |
| Catch-up burst | The first tick takes the edges; motion is shared out across the burst's ticks. Later ticks see held state and no edges — a press fires once, not once per tick. |
| Tap inside one frame | `Press` then `Release` on the next tick, the action never reading as held. |
| Context activated over a held key | Held, never a synthesised press. |
| Context deactivated while holding | `InterruptedRelease`; the action cannot stay held. |
| Second control pressed while the action is held | No second press. Edges describe the action, not the binding that moved. |
| The profile stops resolving | Every held action falls interrupted once and then reads as zero, on both clocks. |
| Focus loss | The capture layer releases held device state as synthetic transitions, which arrive as interruptions. |
| Simulation resumed after a pause | Input made during the pause is dropped from the simulation clock; interruptions are kept. |

Console and UI capture do not release anything: a surface consuming input reports
itself in `InputFrame::UiCapture`, and contexts decide what gameplay hears.

## Hot reload

Both subtypes reload in place while the game runs, and a game resolves its action
names once at startup. An id therefore has to keep meaning the same action across
a reload: the registry keeps each name's id, so reordering the action set is free
and adding to it does not disturb what is already resolved. A name the set stops
declaring retires its slot rather than lending it to whatever was authored next,
so a cached id for a deleted action reads as absent instead of as someone else's
action.

A profile that names a different action set rebinds against that one — the lease
and the reload version it watches follow the path the profile names.

## Diagnostics

A rebind that produces no tables at all keeps the previous ones, so a bad
hot-reload does not take the player's controls away while the bad edit is on
disk. `InputBindingCache::Status()` says which of the four states a profile is in
(`Unbound`, `Current`, `Stale`, `Failed`) and carries the diagnostics.

Those diagnostics are entry state, not a one-shot delivery: a game asking at
startup and the resolve system asking every frame both see them. The resolve
system is the one place that reports, once per change rather than once per frame,
to the log and to `InputActionState::Error()`. That includes a *partial* failure,
where one binding is dropped and the rest of the profile binds — the case that
would otherwise stay silent precisely because the controls mostly work.

Load-time mistakes — duplicate action names, unknown controls, duplicate context
priorities, incomplete composites, a control that cannot produce its action's
value — fail the asset with an exact JSON path rather than producing a control
that silently never fires.

## Boundaries this leaves open

**AbilityKit.** A game bridges actions to `AbilityActivationQueue` in its own
system, which is what keeps one activation path for players and AI (abilitykit
D-I). Which moment an action fires on is settled here, in the mapping, not
there. What remains for the AbilityKit stage is binding an ability definition
directly to an action id, and abilities whose shape spans several moments —
charge while held then loose on release, tap versus hold, timed sequences. Those
need more than one moment and so read the raw edges; the tick records already
carry the edges and tick stamps they will need.

**The application shell.** Which actions open and drive a menu, and what a pause
does to the contexts underneath it, are in [pause.md](pause.md). The mapper's
only part in it is that the shell reads actions like anything else does.

**Networking.** Tick records are flat, tick-stamped, action-indexed value arrays
plus the ordered edge list, with no strings, pointers, or platform types. The
player command (`net/NetPlayerCommand.h`) carries each tick's values and edges for
up to 32 actions, newest first with a redundancy window; when the authority sheds
backlog it concatenates the shed ticks' edges in order rather than merging them,
and a starved tick repeats held state with no edges.

Note that accumulated view angles are *not* in the record. The mapper produces
per-tick look displacement; the entity's `LookOrientation` holds the running
total. Moving absolute angles into the command is a networking decision — the
deltas that would drive them are already per-tick, so it does not need a change
here first. See "Aim" below.

## Aim

Where an entity is looking is `LookOrientation` (`controller/LookOrientation.h`):
accumulated yaw and pitch plus the pitch limits, on the entity doing the aiming.
`LookIntegrationSystem` integrates the look action into it during `PreSimulate`,
for entities tagged `LocalLookControl`; `LookInputBinding` names which action
turns them.

Who carries `LocalLookControl` is the game's rule, not the engine's. The engine
publishes which entity this machine drives (`LocalControlSubjectOf`) and adds
nothing to it; a first-person game moves the tag to follow that subject
(`templates/fps/src/LocalLookFollow.cpp`), a third-person game never adds it
because its body does not aim.

This deliberately does not live in the input layer. Input measures a device and
produces displacement; the running total is simulation state with several
readers — a character steers along it, a camera presents it, an AI could write it
instead of the player. A game's camera system is one of those readers: the
template's first-person camera takes its pitch from the body's orientation.

Look integrates on the presentation clock, because aiming has to track the rate
frames arrive or it visibly steps. Simulation therefore reads a frame-clocked
value, which is correct for local play and is exactly what a replayable command
has to replace: the command carries the orientation sampled for its tick, and
replay feeds that back instead of re-reading the component.

## Deferred

An in-game rebinding UI (the editor's control enumeration and press-to-bind
mapping are engine-side precisely so one can reuse them), a live binding-test
panel that shows action values responding to real devices while authoring,
per-device profile overlays, chords and timed sequences,
input recording and replay, and per-player device routing for split-screen (one
abstract pad is shared by every open device today).

A binding that cannot resolve is dropped and the rest of the profile still
binds, so one mistake costs the controls it is in rather than every control.
The failures that leave nothing bindable at all -- a missing action set, a
vocabulary with duplicate names -- still fail outright.
