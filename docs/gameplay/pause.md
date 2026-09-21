# The application shell

Status: current architecture (2026-09).

Backing out of gameplay is something a Sencha application has, not something
each game assembles. The runtime composes it: a game that writes no pause code,
declares no action and registers no context still presses Escape and gets a
working menu. What a game does instead is edit the model.

```
ui.back  ──> BackRouter ──> the innermost thing that wants it
                            (console, a text field, game UI, ... )
                        └─> nothing wanted it, so: PauseMenu
                                 │
                                 ├─> PauseState  ──> input suspension
                                 │                   simulation suspension
                                 │                   pointer-capture release
                                 └─> page stack ──> UiService
```

## What owns what

| Concern | Owner |
|---|---|
| Is local play suspended, and everything that flips with it | `PauseState` (`app/PauseState.h`) |
| May this process suspend its simulation | `PauseState::Effective()` |
| Which page is on top, and what Back means there | `PauseMenu` (`app/PauseMenu.h`) |
| Who gets Back first | `BackRouter` (`app/BackRouter.h`) |
| What the entries are and what each does | `PauseMenuModel` |
| What settings are offered | `OptionsPage` |
| Suspending gameplay controls | `InputContextSet::SetSuspended` |
| Applying pointer capture | `Engine::ApplyPointerCapture` |
| Whether simulated time advances | `RuntimeFrameLoop::SetSimulationSuspended` |
| Ending the process | `Engine::RequestExit` |

None of this is an ECS component. Pause is a property of the session rather than
of an entity, and a component with exactly one instance is not a component. The
single ECS participant is `PauseInputSystem`, because reading a mapped action
means reading `InputActionState` out of the `World` on the right clock.

## The shell's own input vocabulary

The engine declares `ui.back`, `ui.accept` and the four directions, with default
bindings (Escape and Start; Return and South; arrows and the d-pad), and merges
them into whatever profile a game binds — including into no profile at all,
which is what lets a game shipping no input content still have a menu.

They are ordinary mapped actions, not a side channel. Nothing in the engine
reads a scancode.

`ui.` is reserved. An action set may redeclare one of these and only these, with
a matching type and scope; any other `ui.`-prefixed name fails the asset rather
than quietly minting a second action nothing drives. Redeclaring one is how a
game rebinds it:

```json
{ "name": "ui.back", "type": "digital", "scope": "presentation" }
```
```json
{ "action": "ui.back", "control": "key.p" }
```

Three rules make that work, and the third is load-bearing:

- **Identity stays the engine's.** A redeclaration binds to the engine's id; it
  never allocates a second one, so an id resolved once at startup stays correct.
- **Authored bindings replace the defaults, per action.** Replace rather than
  augment: a player who moves Back to another key must not find Escape still
  working.
- **A binding for a `ui.*` action compiles into the shell's context**, whichever
  authored context declared it. Authored contexts stop resolving while the shell
  has input suspended, so a Back left where the author wrote it would go silent
  the instant the player paused — and there would be no way to resume.

The shell's context sits above any authored priority, and an authored context at
or above that band is refused. It is not lease-managed: nobody activates it, a
game cannot deactivate it, and suspension does not reach it.

## Back is routed; pause is the fallback

`BackRouter` is `PlatformEventRouter`'s shape — named consumers, offered in
order, first to claim it ends the offer — with two additions dynamic UI needs.

**Lifetime is a lease.** A screen registering a lambda that captures `this` and
then being destroyed would leave a dangling call; dropping the lease
unregisters. Same answer `InputContextSet` gives to the same question.

**Ordering is bands, then most-recent-first inside `Surface`:**

| Band | Occupant |
|---|---|
| `Diagnostics` | the debug console |
| `TextEntry` | an in-progress text edit |
| `Shell` | the pause page stack, while it is open |
| `Surface` | game UI layers, innermost first |
| `Fallback` | opening the shell from gameplay |

Registration order alone is wrong for nested UI: an inventory registered at
startup that later spawns a modal must not out-rank the modal it spawned.

The console's band is required for correctness, not politeness. The platform
router folds every event into the input snapshot *before* offering it to
consumers, so the console claiming Escape at the event layer does not stop
`ui.back` firing — the mapper resolves from the snapshot afterwards. It has to
claim it here too.

`PauseMenu` holds two leases: `Fallback` permanently, so "Back opens the menu"
is the absence of anyone else wanting it, and `Shell` while a page is open, so an
inventory left open behind a menu cannot take the player's Resume press.

## Suspension is its own fact

```cpp
enum class PausePolicy { SuspendSimulation, InputOnly };
```

**Pause does not write the timescale.** `RuntimeFrameLoop` carries suspension and
rate as two facts with one owner each: the shell suspends, `time.timescale` sets
the rate, and neither can overwrite the other. Recording the rate and restoring
it later would be a last-writer-wins rule — a game that legitimately changed
speed while paused would find the change reverted on resume.

**Session shape decides the effective policy, not authority.** `SuspendSimulation`
is honoured when there is no live session; with one — client *or* host — the
effective policy is `InputOnly`. A host does own its simulation, so an authority
rule would let it freeze the match while the menu claimed to be a local thing,
and would make correctness a per-game chore. A game that wants a host pause to
stop everyone writes `time.timescale` itself, which is replicated.

"Live" means hosting or joined: `NetSessionRole::Standalone` is a real state for
an idle session and does not count.

## The frame

`ScheduleFixedTicks` decides a frame's tick budget, and `PauseInputSystem` runs
after it in the same phase. That is the one window in which a frame still has
ticks to cancel.

| Frame | Phase | |
|---|---|---|
| N | `ScheduleTicks` | budget computed; the mapper resolves; `PauseInputSystem` dispatches Back |
| N | | `PauseState::Apply()` → `Entered`: suspend input, suspend simulation, **cancel this frame's remaining ticks** |
| N | `Simulate` | budget is zero — the loop body never executes |
| N | `Update` | the menu opens its root page; the capture arbiter is re-driven; `UiService::Update` follows |
| N | `Render` | the menu draws **in the frame Escape was pressed**, over a cursor already released |

**Cancellation follows the transition, never the keystroke.** Back is not a
synonym for pause: an inventory closing on it must leave the frame simulating,
so only `Apply()` returning `Entered` cancels.

Resume is the mirror, and two things have to happen that are easy to miss:

- **The simulation input latch is discarded.** While suspended no tick runs, so
  nothing drains that latch — but every frame still folds its transitions and
  motion into it. A minute in a menu accumulates a minute of pointer travel and
  every button edge, including the click that pressed Resume, and the first tick
  back would take the lot. Leaving the paused state marks a
  `SimulationPause` discontinuity, which the mapper subscribes to.

  Only the simulation clock, and only that reason. The presentation clock drains
  every frame, and a world event has no business swallowing a UI click.

- **The pointer's jump is discarded.** Toggling relative mouse mode teleports the
  cursor, and the platform reports that jump the only way it reports movement.
  `PointerCaptureSettle` drops pointer motion for two frames around an applied
  capture change — two because the change can land either side of the platform
  pump, and the motion arrives on the pump after that.

Both are needed and they cover different frames. Neither is caused by pausing:
alt-tabbing back and a console closing over a game do the same thing.

### A trap worth knowing

`RuntimeFrameLoop::EndFrame` used to clear `DiscontinuityPending`. Every caller
that predated the shell marked a discontinuity *before* `ScheduleFixedTicks`, so
the flag was always consumed in the frame it was raised and clearing it looked
free. It was not: anything recognising one after the budget was decided had its
signal dropped without a word. A pending discontinuity — and its reason — now
survives to the next boundary, which is what "pending" always said it would do.

## What stops and what continues

Under an effective `SuspendSimulation`:

**Stops**, because the tick budget goes to zero: `FixedLogic`, `Physics`,
`PostFixed`, simulation-domain transform propagation, `WorldTransformHistory`
capture. Jolt is not stepped with dt 0 — it is not stepped. Every gameplay
timer, because all of them accumulate `FixedSimTime::DeltaSeconds`:
`ActiveEffect::TimeRemaining` (which ability cooldowns ride),
`JumpState::CooldownRemaining`, animation playback. **Cooldowns do not expire
while paused, for free.** Host snapshot publishing also stops, since
`ReplicationRuntime::Publish` gates on a tick difference — which is why a live
session degrades to `InputOnly`.

**Continues**, because it is driven per frame: the platform pump, swapchain
rebuild, async commits and scene spawns, zone residency and streaming (on
unscaled wall time, so a zone can still linger out during a long pause), network
receive and transmit including keepalives and timeouts, input resolution on the
presentation clock, frame-update systems, audio, extraction and render. The
world still draws behind the menu.

**UI animation is unaffected by construction** — the document engine runs its own
`steady_clock`, so menu transitions never share game time.

**Audio keeps playing.** Which buses quieten under a menu is a game's mix
decision via `AudioService::SetBusVolume`/`SetBusMuted`; the shell takes no
position.

## Customising the menu

Authoring is by stable id. Positions are what the document repeats over, never
what a caller addresses — a menu reordered by index is one where every later
edit has to know what moved.

```cpp
PauseMenuModel& m = engine.TryPauseMenu()->Model();
m.SetLabel(kPauseResume, "Continue");
m.MoveBefore(kPauseExit, kPauseOptions);
m.Remove(kPauseOptions);
const PauseCommandId save = m.Add("Save Game", [](PauseMenuContext& ctx) { ... });
m.SetHandler(kPauseExit, [](PauseMenuContext& ctx) { ctx.Menu.Push(ConfirmQuit()); });
m.SetRootPage("asset://ui/my_pause.sui");
```

The document repeats over the published labels and reports which row was
activated; that index is resolved back to a command before anything acts on it.
So the markup names no command, and adding, renaming or reordering an entry is a
change to the model and never an edit to a document. The row-to-command
relation the document is repeating over is captured when the labels are
published, so a click queued before a reorder still means the row that was
pressed.

**An entry's behaviour is native or authored, never both.** The stock Resume
and Exit entries take theirs from `engine/assets/data/shell.bindings.sdata`
(`shell.resume`, `shell.quit`), records that name the engine's `runtime.resume`
and `application.quit` verbs; the model holds the binding key
(`PauseMenuEntry::Binding`) and never the verb. `SetHandler` on such an entry
replaces the authored behaviour; `SetBinding` on a native entry replaces the
handler. A game gives an entry a binding from its own asset by appending that
asset's records to `Engine::ShellBindings()` from `OnStart`:

```cpp
engine.ShellBindings().Append(*library, MakeVerbBindingEnvironment(world), errors);
const PauseCommandId award = m.Add("Award red a point", {});
m.SetBinding(award, MakeVerbBindingKey("arena.award_red"));
```

Options stays a native handler: page navigation is a controller relation, not
a verb.

**Pages own what their rows mean.** A page carries its own `Publish`, `Activate`
and `Closed`, so the settings page's knowledge of settings never reaches the
menu. `Closed` is the commit boundary: a value nudged a dozen times on the way to
the one the player wanted is one write, not a dozen.

Two pages on one surface **cannot share a model name** — a document context holds
one model per name, and the second silently fails to open.

## Leaving

Every *graceful* source reaches one gate: the window button and Alt+F4 as much
as the menu's own entry, the console `quit`, and a game's own call.

```cpp
engine.OnExitRequested = [](Engine::ExitSource source) {
    return Engine::ExitDecision::Defer;   // and later ConfirmExit() / CancelExit()
};
```

A deferred request is pending, and further requests coalesce into it rather than
re-entering the handler — so holding Alt+F4 with a confirmation up cannot stack
one dialog per event. A renderer that failed and a signal the process was sent do
not reach the gate: those are notifications that this is ending, not requests,
and a veto there would be a hang.

The command is platform-neutral; the label is the host's. A desktop says "Exit to
Desktop"; a host that cannot terminate passes no label and gets no entry, rather
than an entry that refuses.

## Options

Not a special concept: a second page, on the stack Back already walks. The entry
appears only once a page exists, so it is never a button that does nothing.

The page is a **curated table**, not a listing of every archived cvar — the ones
carrying `Archive` are mostly engine tuning, and a page over all of them would be
a developer panel wearing a player's name. Four rows ship:

| Row | Cvar | Control | |
|---|---|---|---|
| Master Volume | `audio.volume` | slider 0–1 | multiplies over the game's own bus mix without overwriting it |
| Look Sensitivity | `input.look_sensitivity` | slider 0.25–3 | multiplies whatever the binding was authored with |
| Display Mode | `window.mode` | drop-down | Windowed / Borderless / Fullscreen |
| Frame Rate Cap | `r.target_fps` | drop-down | Unlimited / 30 / 60 / 90 / 120 / 144 / 240 |

**Each shell document has a preview model beside it** -- `pause.preview.json`,
`options.preview.json` -- the declaration and sample values the host would
publish, so Shoji (`editor/shoji/`) shows the page as the game does without the
game running, and `UiPreviewModelTests` checks the sidecar against
`OptionsPage::Describe` so preview and host cannot drift. A binding the document
uses and the sidecar lacks shows up in Shoji as a `binding missing` diagnostic
naming the variable; the blank-label bug a `{{row.Label}}` against a member
registered as `label` produces is that row, on save. Note the attribution rule
from `docs/ui/architecture.md` §13: a miss in body text names the screen, a miss
inside a `data-for` row names only the surface.

The document owns the control; `OptionsPage` owns what its value means. A row
is a `UiRow` carrying `Control`, `Min`/`Max`/`Step` and `Choices` beside its
label and value, and the document offers a `<input type="range">` or a
`<select>` accordingly. A `Choice` row shows **labels**: `OptionRow::Choices`
maps each label to the cvar value it stands for, so "Unlimited" is what the
player reads and `0` is what the console gets. A current value no label stands
for is presented as itself and appended to the choices, so the drop-down can
select it rather than fall back to its first entry and report that as a change.

Two facts about RmlUi decide the shape of the wiring:

- **The action carries the value.** Both controls raise
  `options_activate(it_index, ev.value)`. A change event reaches the `data-value`
  controller (which writes the model) and the `data-event` controller (which
  raises the action) in an order the engine does not define, so the host never
  reads the row back inside the action; the value travels with it, typed — a
  float from a slider, a label from a drop-down.
- **A change fires on publish, not only on input.** Opening the page raises the
  action once per control with the value it was just given. `OptionsPage::Apply`
  therefore compares against what `Present` shows — the snapped number, the
  current label — and writes nothing when they agree. Opening the page is a
  fixed point even when the stored value sits off the slider's step.

Slider changes apply live, per drag tick; the settings file is still written
once, when the page closes.

**A row exists only if its cvar does**, and each cvar is registered only where
the thing behind it exists. A headless host registers no display mode; a host
with no mixer registers no volume. So what a host cannot do is not offered,
rather than offered and refusing, with no branch on host kind anywhere.

Control rebinding is **not** here. `docs/gameplay/input.md` lists an in-game
rebinding UI as deferred, and the data editor's press-to-bind capture is
engine-side so it can be reused when that lands.

## Settings persistence

`CVarArchive` writes the `Archive` cvars that differ from their defaults to one
file, and applies them at startup — after the engine's own cvars register and
**before** the startup script, so precedence reads command line, then saved
settings, then defaults. A name no cvar claims yet is queued rather than dropped,
so a game module's setting registered in `OnStart` is still applied.

**Where the file lives is decided by two parties, and a default configuration
persists nothing.** The host names a directory in
`EngineConsoleConfig::SettingsRoot` — `app` resolves the platform's
configuration directory (`platform/UserPaths.h`, `~/.config/sencha` on Linux);
a test, a tool or an editor leaves it empty and can never touch a user's disk.
The game names itself in `App.Name` from `OnConfigure`, and `Engine` composes
`CVarArchive::FileFor(root, name)` after that hook, the first point where both
are known. **`App.Name` is the settings namespace**: renaming a game moves its
settings, and two names that slug alike share a file. `app --settings <dir|none>`
overrides the root for a launch; a headless host defaults to `none`, so a
server's frame cap is the operator's and not whoever last played on the machine.

## Which hosts have a shell at all

`EngineRuntimeConfig::ApplicationShell` is the host's declaration that a player
sits in front of this process. It gates the shell's composition — the surface,
the pause menu, the options page — and the registration of `PauseInputSystem`,
the one system that reads the shell's Back action. The four editors and a
headless `app` set it false. Nothing else is gated on it: player-setting cvars
are console tunables and exist regardless, and the engine's built-in content
mounts for any host with a UI layer. "Has a window and some UI packages" was
never a declaration of anything, which is how an editor came to boot a pause
menu.

The file is a record of assignments, not a script: the same JSON shape
`engine.json`'s `cvars` section uses, read through the registry's ordinary set
path. There is no route by which an entry becomes a console *command*, which
matters for a file in a user-writable directory. Values are written as native
JSON scalars, so a double reloads as the number that was saved.

Saving happens at explicit commit boundaries — the options page closing, and
shutdown — never from a frame phase.

## What a game does

Nothing, to get a menu. To change one:

```cpp
void MyGame::OnStart(GameStartupContext&)
{
    GetEngine().SetPointerCaptured(true);   // gameplay owns the mouse while playing
    if (PauseMenu* menu = GetEngine().TryPauseMenu())
        menu->Model().SetLabel(kPauseResume, "Continue");
}
```

`SetPointerCaptured` is a standing intent, stated once. The shell releases the
pointer while a menu is up and takes it back on resume; the platform layer drops
it on focus loss and restores it after. A game never has to notice any of that.

The templates carry no pause code and handle no platform events. The hold-right-
button-to-look convention they used to share is gone.

## Related

| Doc | Relationship |
|---|---|
| `docs/gameplay/input.md` | the action mapping, contexts, and the clocks this reads |
| `docs/ui/architecture.md` | the surfaces, screens and semantic actions the pages are built on |
| `editor/ARCHITECTURE.md` | Shoji, the previewer the shell documents are authored against |
| `docs/core-systems-map.md` | the frame phases the transitions sit in |
