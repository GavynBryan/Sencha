# participant

Snapshot `97ed48e4` (2026-10-30). Layer: network.

## 1. Frame phases

- None of its own: `ParticipantLifecycle` (`engine/include/participant/ParticipantLifecycle.h:98`) is called by games and by app's session handling when players join, leave or take control.

## 2. Inputs

- Admission and control requests from the game or session, through `ParticipantLifecycle` (`engine/include/participant/ParticipantLifecycle.h:98`).
- Policies, `ParticipantPolicies` (`engine/include/participant/ParticipantLifecycle.h:13`).
- Module dependencies: ecs, input, world.

## 3. Outputs

- `ParticipantControl` (`engine/include/participant/ParticipantControl.h:13`) and `LocalParticipant` (`engine/include/participant/ParticipantControl.h:24`): who a participant is and what it drives, consumed by app, participant_spawn and six game-template files.
- `LocalControlSubject` (`engine/include/participant/LocalControl.h:9`): the entity this machine's player drives; set by app (`engine/src/app/SessionParticipantProjection.cpp`) and read through `LocalControlSubjectOf` by navigation's console and the game templates.
- Diagnostics, `FormatParticipantStatus` (`engine/include/participant/ParticipantDiagnostics.h:39`): consumed by `engine/src/app/SessionParticipantDiagnostics.cpp`.

## 4. Central decisions

- `ParticipantLifecycle::RequestBody` (`engine/src/participant/ParticipantLifecycle.cpp:221`): admits or refuses a body request for a participant.
- `ParticipantLifecycle::SetControlSubject` (`engine/src/participant/ParticipantLifecycle.cpp:173`): moves control to a subject and displaces its previous driver.
- `ParticipantLifecycle::Bind` (`engine/src/participant/ParticipantLifecycle.cpp:274`): binds an existing entity as a participant's body, or refuses.

## 5. Invariants

- The participant mechanism has no network dependency: `ParticipantBoundary.ParticipantMechanismHasNoNetworkDependency` (`test/participant/ParticipantBoundaryTests.cpp`).
- Taking a subject clears its previous participant: `ParticipantLifecycle.TakingASubjectClearsItsPreviousParticipant` (`test/participant/ParticipantLifecycleTests.cpp`).
- A refused assignment destroys nothing and changes nothing: `ParticipantLifecycle.ARefusedAssignmentDestroysNothingAndChangesNothing` (`test/participant/ParticipantLifecycleTests.cpp`).
- Retirement reaps the body but never the driven subject: `ParticipantLifecycle.RetirementReapsTheBodyButNeverTheDrivenSubject` (`test/participant/ParticipantLifecycleTests.cpp`).
- Pure operations add no network components: `ParticipantLifecycle.PureOperationsAddNoNetworkComponents` (`test/participant/ParticipantLifecycleTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 893 lines in 8 files.
- CCN 13 `ParticipantLifecycle::RequestBody` (`engine/src/participant/ParticipantLifecycle.cpp:221`); CCN 12 `ParticipantLifecycle::SetControlSubject` (`engine/src/participant/ParticipantLifecycle.cpp:173`); CCN 12 `ParticipantLifecycle::Bind` (`engine/src/participant/ParticipantLifecycle.cpp:274`).

## 7. Docs that contradict the code

- None found.
