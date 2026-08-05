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
    { "name": "jump", "type": "digital" },
    { "name": "pause", "type": "digital", "scope": "presentation" }
]}}
```

`type` is `digital`, `axis1`, or `axis2`. `scope` is `simulation` (the default —
the action drives the simulation and may travel in a player command) or
`presentation` (local to this client: menus, debug toggles).

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

Sticks and triggers are *positions*, not displacement: they hold their value
until the device moves, so every tick of a catch-up frame reads the same stick.
Only mouse motion and the wheel accumulate.

Both subtypes are runtime formats: no cook step, and the existing `.sdata`
watcher hot-reloads them in place while the game runs.

## Reading actions

```cpp
// Resolved once at startup: names to dense ids.
struct TemplateInputActions { InputActionId Move, Look, Jump; };

void CharacterInputSystem::FixedLogic(FixedLogicContext& ctx)
{
    const auto* ids = ctx.Entities.TryGetResource<TemplateInputActions>();
    const auto* actions = ctx.Entities.TryGetResource<InputActionState>();
    if (ids == nullptr || actions == nullptr)
        return;

    const InputActionView input = actions->Tick();
    const Vec2d move = input.Axis2(ids->Move);   // strafe on X, forward on Y
    const bool jump = input.Held(ids->Jump);     // or Pressed() / Released()
    ...
}
```

`Tick()` is this tick's record; `Frame()` is the presentation snapshot, which is
what a camera or a menu wants. A system that reads actions must be ordered after
the resolve system:

```cpp
ctx.Schedule.After<CharacterInputSystem, InputActionResolveSystem>();
```

Adding an action costs one entry in the action set, one binding in the profile,
and one field wherever it is consumed. No engine edit, no central switch.

## Contexts

A context is a named group of bindings that can be turned on and off at runtime.
Activation returns a lease; the context stays live until the lease is dropped, so
a menu that is torn down or a dialogue interrupted by a load cannot leave its
context stuck on. Leases are counted, so two holders do not cancel each other.

```cpp
InputContextLease menu = world.GetResource<InputContextSet>().Activate("menu");
// ... dropping `menu` deactivates it
```

Every context has a unique priority within its profile. When several active
contexts bind the same control, the highest-priority one claims it and the
control reads as absent to everything below — per control, not per context, so a
menu that binds Escape does not also take movement. To block a whole context,
deactivate it; there is no separate "consume everything" flag.

## Frame and tick semantics

A rendered frame may run zero, one, or several fixed ticks against one sample of
input. The mapper keeps a separate latch per clock, which settles every case:

| Situation | Behaviour |
|---|---|
| Zero ticks | The presentation snapshot still resolves. The simulation latch keeps the edges and motion until a tick runs, so nothing is dropped. |
| One tick | The tick consumes everything latched since the previous tick. |
| Catch-up burst | The first tick consumes the latch, including the full accumulated motion. Later ticks see held state, no press edges, and no motion — a press fires once, not once per tick. |
| Tap inside one frame | Pressed and Released arrive together on the next tick, with the action never reading as held. |
| Context activated over a held key | Held, never a synthesised press. |
| Context deactivated while holding | The action releases; it cannot stay held. |
| Focus loss, or the console taking input | The capture layer releases held device state, so actions release through the ordinary path instead of sticking down. |

Context changes are applied at the frame boundary, before the first tick, so
every tick of one frame resolves against the same set.

## Diagnostics

A profile whose bindings do not resolve is reported and keeps its previous
tables, so a bad hot-reload does not take the player's controls away. The failure
is logged once and left on `InputActionState::Error()`. Load-time mistakes —
duplicate action names, unknown controls, duplicate context priorities,
incomplete composites, a control that cannot produce its action's value — fail
the asset with an exact JSON path rather than producing a control that silently
never fires.

## Boundaries this leaves open

**AbilityKit.** A game bridges actions to `AbilityActivationQueue` in its own
system, which is what keeps one activation path for players and AI (abilitykit
D-I). Binding an ability definition directly to an action id, and hold/release
ability semantics, are deferred to the AbilityKit tasks stage; the tick records
already carry the edges and tick stamps those will need.

**Networking.** Tick records are flat, tick-stamped, action-indexed value arrays
with no strings, pointers, or platform types. A command builder projects the
actions a game replicates out of a record by index. This ticket supplies that
shape and nothing else: no schema, no redundancy window, no replication.

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

This deliberately does not live in the input layer. Input measures a device and
produces displacement; the running total is simulation state with several
readers — a character steers along it, a camera presents it, an AI could write it
instead of the player. `CameraRig` is one of those readers: it carries the target
relationship and boom shape, and `ComputeCameraPose` is passed the orientation.

Look integrates on the presentation clock, because aiming has to track the rate
frames arrive or it visibly steps. Simulation therefore reads a frame-clocked
value, which is correct for local play and is exactly what a replayable command
has to replace: the command carries the orientation sampled for its tick, and
replay feeds that back instead of re-reading the component.

## Deferred

A rebinding UI, per-device profile overlays, chords and timed sequences,
input recording and replay, and per-player device routing for split-screen (one
abstract pad is shared by every open device today).

Analog-to-digital thresholds are also deferred: binding `jump` to
`gamepad.right_trigger` is rejected at load rather than treated as a button,
because a threshold crossing has no device edge behind it and would need its own
per-clock previous-value state. Bind a button, or add the threshold when a game
needs it.
