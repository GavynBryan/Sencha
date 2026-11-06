# ui

Snapshot `97ed48e4` (2026-10-30). Layer: presentation.

## 1. Frame phases

- `Update`: `Update` (`engine/src/app/EngineFramePhases.cpp:903`) applies what hosts published and lays out, explicitly after the FrameUpdate lane so an action shows the same frame.
- `PumpPlatform`: events reach `ProcessPlatformEvent` (`engine/include/ui/UiService.h:183`) through the platform event router.
- `Render`: extracted UI geometry is drawn by graphics' UI pass.

## 2. Inputs

- Cooked UI packages, opened by `OpenScreen` (`engine/include/ui/UiService.h:148`).
- Values hosts publish to screens, and semantic actions from input.
- Platform events, through `ProcessPlatformEvent` (`engine/include/ui/UiService.h:183`).
- Module dependencies: assets, authored, core, graphics, input, math, platform, render.

## 3. Outputs

- `UiService` (`engine/include/ui/UiService.h:45`): screens, surfaces and actions, consumed by app, render's UI feature and 59 editor files.
- Laid-out geometry for the UI draw pass: consumed by graphics.

## 4. Central decisions

- `UiRuntime::ProcessPlatformEvent` (`engine/src/ui/UiRuntimeInput.cpp:44`): routes one platform event to the topmost surface that takes it, or declines.
- `UiRuntime::RebuildScreen` (`engine/src/ui/UiRuntime.cpp:768`): rebuilds a screen's document from its package and bindings.
- `UiRuntime::OpenScreen` (`engine/src/ui/UiRuntime.cpp:485`): opens a screen on a surface, or reports why it could not.

## 5. Invariants

- Events are offered to the topmost surface first: `UiInput.EventsAreOfferedToTheTopmostSurfaceFirst` (`test/runtime/UiInputTests.cpp`).
- Navigation comes from the host's actions, not from keys: `UiInput.NavigationComesFromTheHostsActionsNotFromKeys` (`test/runtime/UiInputTests.cpp`).
- Closing a screen takes the actions it had not delivered yet: `UiInput.ClosingAScreenTakesTheActionsItHadNotDeliveredYet` (`test/runtime/UiInputTests.cpp`).
- Typing changes the presentation value and nothing else: `UiControls.TypingChangesThePresentationValueAndNothingElse` (`test/runtime/UiControlTests.cpp`).
- Closing a screen mid-edit commits nothing: `UiControls.ClosingAScreenMidEditCommitsNothing` (`test/runtime/UiControlTests.cpp`).
- A missing binding names the variable, the surface and the only screen: `UiDiagnostics.AMissingBindingNamesTheVariableTheSurfaceAndTheOnlyScreen` (`test/runtime/UiDiagnosticsTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 6,463 lines in 38 files.
- CCN 60 `UiRuntime::ProcessPlatformEvent` (`engine/src/ui/UiRuntimeInput.cpp:44`); CCN 23 `UiRuntime::RebuildScreen` (`engine/src/ui/UiRuntime.cpp:768`); CCN 18 `UiRuntime::OpenScreen` (`engine/src/ui/UiRuntime.cpp:485`).

## 7. Docs that contradict the code

- `docs/ui/architecture.md:216` names the engine's raw-input readers `ExitOnEscape` and `TogglePauseOnF1`; neither exists in the tree.
