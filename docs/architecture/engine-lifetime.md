# Engine lifetime and teardown

How a runtime process brings the engine up and takes it down, and which owner
gives back which state. Teardown order is a contract here, not a consequence of
member declaration order: a member left for the destructor runs after every
explicit reset above it, so anything it borrows from them is already gone.

## Scopes

| Scope | Opened | Closed | Holds |
|---|---|---|---|
| Process | `app` `main` loads the game module | `loader.Unload` after `Application::Run` returns | the module mapping |
| Engine | `Engine::Initialize` | `Engine::Shutdown` (also from `~Engine`) | logging, debug, console, audio, platform and windows, graphics and the renderer, job and task lanes, the frame driver; also the world tier `Run` composes: `RuntimeWorld`, spawn services, the authored dispatchers, scheduled systems |
| Run | `Engine::Run`, after the schema seals | the tail of `Engine::Run`, after `Game::OnShutdown` | the content stack and level, the vocabulary bindings, the shell's operations, the UI service, the settings store, the application shell (`PauseMenu` and its surface), and the authored UI render feature |

The world tier is composed by `Run` but closed by `Shutdown`, because scheduled
systems hold it and are shut down there. Everything in the run scope borrows the
content stack or the UI service, so it is released in `Run`'s tail, in reverse
dependency order, while those owners still live. `Engine::RunScopeReleased`
names the run scope, and `Shutdown` asserts it: a run-scope object left for
member destruction is a use-after-free, not a style problem.

## Run's teardown order

1. Verb and event admission closes; open pause pages close while their
   game-owned callback state still lives; their `Closed` commits finish.
2. Settings are saved after page commits, but before `Game::OnShutdown`,
   so shutdown-only cvar writes do not reach the archive.
3. `Game::OnShutdown` -- the game releases its render features, leases and
   tokens while their providers are still live.
4. The shell's bindings and operations, then the shell and its surface go.
5. The authored UI render feature is removed from the renderer
   (`Renderer::RemoveFeature` drains the GPU first). A dependent feature
   left registered is an owner-lifetime violation: shutdown terminates rather
   than releasing the UI service and texture cache beneath a live borrower.
6. `UiService::Shutdown`, while the asset caches its screens lease from live.
7. Task lanes stop; the level unloads; the content stack disconnects, the game's
   data-asset subtypes are withdrawn while the module is mapped, and the stack is
   destroyed; then the UI service and settings store.
8. The game's serializers and component schema entries are retracted, still
   inside the module's mapping.

`Engine::Shutdown` then shuts down systems, the dispatchers, the network
session, the frame driver and job lanes, the world tier, graphics, platform,
audio, console and logging, in that order. `Run` may be called again after
`Shutdown`; every run composes and releases its own run scope.

## Ways out

Every graceful exit leaves the frame loop the same way and runs the same
teardown: the shell's Exit entry, the window's close, the console's `quit` and a
game's call all pass through `Engine::RequestExit`; `app.exit_after_frames` ends
the loop from the frame driver's exit predicate. A windowed host receiving
SIGTERM or SIGINT gets SDL's default handlers, which post a quit event that
arrives as `ExitSource::WindowClose`, so a signal is an ordinary, deferrable
exit there. A headless host installs its own handlers (`app/main.cpp`), which
stop the loop without consulting the exit gate.

Kyusu's Play stop (`PieSession::Stop`) sends SIGTERM, so a stopped session runs
the same teardown as a quit. It waits `PieSession::kStopGrace` for the child,
then kills it; a game holding the exit open past that is killed. A child that
dies by any signal other than SIGTERM, exits non-zero, or has to be killed is
reported through `TakeExitReport`, so a crash during a stop is shown rather than
swallowed.

## Diagnosing a teardown crash

The last log line before a teardown crash is not evidence of where it happened:
`Shutdown` clears the log sinks before the engine's members are destroyed, and
writing the core dump takes time of its own. Use the core
(`coredumpctl info <pid>`, `coredumpctl debug <pid>`) or an `asan` build.
