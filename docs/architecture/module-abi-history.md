# Game-module ABI history

`SENCHA_GAME_ABI_VERSION` (engine/include/app/ModuleExport.h) is bumped on a
deliberate break of the game-module contract or registration surface. Most skew
is caught automatically by the ABI fingerprint, a hash of the module-facing
headers and the component code generator, plus the build-identity record in
GameModuleAbi.h; the integer remains the human-meaningful "intended break"
marker. This is what each version changed.

## v3

Added the GameModuleAbi handshake and the EditorVisual hint on
IComponentSerializer.

## v4

The module factory returns a Game (retiring the IGameModule/GameModuleContext
side-contract); serializer registration is the Game::OnRegisterComponents hook.
(09-module-abi-hardening.md.)

## v5

Added the IsRemovable hint on IComponentSerializer (a new trailing vtable slot).

## v6

Removed Engine::Driver, Engine::ActiveRenderProfileMode, and the
EngineFramePhases.h registration free function, and made the frame-phase
accessors (Timing, Instrumentation, the profiling latch/publish pair) private;
IRenderFeature lost the unreachable Contribute slot; JobSystem became the
concrete pool and IWindow was removed.

## v7

The process-global component serializer registry is gone -- each host owns one
and the scene save/load functions take it explicitly, so a module that reached
for the default registry no longer links. Game::OnRegisterComponents is
unchanged; the registry it receives is now the engine's own.

## v8

Added the Game::OnRegisterDataAssetTypes / OnUnregisterDataAssetTypes pair, so a
module can register structured data subtypes and their authoring schemas.

## v9

The fixed-tick, physics, post-fixed, render-extract, audio, and end-frame
contexts no longer carry an InputFrame. Simulation reads resolved actions from
InputActionState; PreSimulate (where the mapper runs) and FrameUpdate (editor
and debug tooling) still expose the raw device frame.

## v10

Participant lifecycle/control moved out of net into its own SDK domain, the old
NetPlayer and NetParticipant surface was removed, and Engine now exposes typed
participant outcomes through the session projection owner.

## v11

The render feature contract went neutral -- RenderPhase, IRenderFeature, and the
new RenderFeatureServices / RenderFrame types moved to graphics/RenderFeature.h,
and Setup/OnDraw take those instead of the backend RendererServices /
FrameContext (still reachable through their Backend pointers). The fingerprint
now covers graphics/*.h, the neutral shelf.

## v12

GpuFrameScratch allocations name their consumer -- the Allocate family takes a
ScratchTag, so one slice's budget can be attributed per feature instead of only
in aggregate.

## v13

Engine owns SceneSpawnService and exposes it as Spawns() -- runtime scene
spawning over the cooked .smap pipeline; games wire their asset stack via
ConnectAssets in OnStart. Engine gained a member, so accessor offsets moved.

## v14

Added Game::OnRegisterVocabulary (a new trailing vtable slot), so a module
declares its gameplay tags, attributes, abilities, and locomotion modes into any
World -- the runtime's, and each of the editor's authoring documents.

## v15

The engine owns the process's content. Engine gained a RuntimeContent member (so
accessor offsets moved) and exposes it as Content(); it composes the asset
stack, mounts RuntimeConfig::ContentRoots, publishes the world's asset
resources, and connects the spawn services and render pipeline itself. A module
no longer builds a RuntimeAssets, and Game::OnRegisterDataAssetTypes is now
called by the runtime rather than only by the data editor.

## v16

The engine loads levels. Engine gained a LoadedLevel member (so accessor offsets
moved) and exposes it as Level(); map, world, zone, zones, scene.spawn and
scene.despawn are engine console commands, and ConsoleService::SetMapHandler is
gone -- `+map` loads through the engine rather than calling back into a game. A
game learns that a level arrived from the zone-residency changes the load
publishes.

## v17

Participant admission is session-gated.
SessionParticipantProjection::AdmitLocal, AdmitSimulated and RequestBody take
whether a session is active and add replication state only then;
Engine::ProjectSessionStart stamps retroactively when a process starts hosting.
SetLocalControlSubject publishes identity only and no longer adds or removes
LocalLookControl -- a game that wants that rule composes it.

## v18

The engine has no camera policy and no local streaming policy. CameraRig,
CameraRigMode, ComputeCameraPose, CameraFollowSystem, RegisterCameraSystem and
the authored CameraSeat (CSET) are gone; CameraExclusion is the one runtime
camera component, read by extraction, and FirstAuthoredCamera is the one query.
NetZoneStreaming::Update no longer takes a local control subject; a game sets
the partition's primary focus itself.

## v19

RegisterCameraComponents(World&) is gone -- the engine schema gives every world
storage for its components, so the call registered nothing. Render extraction
and presentation-domain transform propagation take the frame's interpolation
alpha: a camera with pose history, or one parented to an entity with it, is
drawn from the blend the meshes are.

## v20

App/BodySpawns owns a prefab body request from the ask until its group is handed
to the participant lifecycle or cleaned up; games install it as their body
policy instead of keeping the book themselves.
SceneSpawnService::IsDespawnRequested reports an ended request before the pump,
and AsyncTaskQueue::Stop ends the cross-frame lane. The engine stops that lane
right after Game::OnShutdown returns, so a module may not submit async work from
its shutdown hook or later.

## v21

The authored UI layer reaches modules. Engine::Ui() hands back a UiService, and
a game opens screens, publishes presentation values and drains semantic actions
through it; the engine drives update, extraction and rendering, and the ui/
headers join the ABI fingerprint because a module holds those handles and values
by value. A screen holds asset leases, so it lives inside the content stack's
span: the engine shuts the UI down right after Game::OnShutdown returns, and a
module must not hold a screen past its own shutdown hook.

## v22

The authored UI facade settles. UiRow carries an Editable flag, so a row says
whether the document should offer a control for its value -- which changes the
type's layout, and a module passes rows by value. UiService::DrainActions takes
a screen, so a host with more than one authored controller stops having them
swallow each other's actions; the no-argument form stays for a host that owns
every screen. With the control vocabulary and two real surfaces built on it, the
facade is frozen: it changes from here under the ordinary rules rather than a
stage at a time.

## v23

The application shell. Engine carries the saved settings store, so its size
changes and a module holding an Engine& has to be rebuilt; EngineRuntimeConfig
loses ExitOnEscape and TogglePauseOnF1, whose raw scancode handlers the shell's
rebindable Back action replaces, which changes EngineConfig's layout and a
module receives one by reference in every lifecycle context. RuntimeFrameLoop
gains a suspension flag separate from the timescale, and InputContextDefinition
a shell flag, both of which a module sees by value through the headers it
already includes. In the same bump: EngineRuntimeConfig gains ApplicationShell
and EngineConsoleConfig gains SettingsRoot, so a host declares its posture and
where settings may live; EngineDebugConfig loses DebugUi, which nothing read;
and UiRow carries control metadata (Control, Min, Max, Step, Choices) beside
Editable, so a row can name the slider or drop-down a document offers for it.

## v24

RuntimeContent's source watching is SourceReloadRoots, the one watcher the
editors use too, so its members change layout and a host that mounts more roots
adds them to RuntimeContent::SourceReload(). In the same bump:
PlatformEventContext carries the event's WindowId and RuntimeFrameLoop gains the
other-presentation flag, for rendering to more than one window; graphics/vulkan
and platform headers join the fingerprint, since a module reaches their members
through Graphics() and Platform().

## v25

**Authored UI for editors that present through it entirely.** Generated-texture
uploads and releases leave the per-surface UiDrawFrame and are handed over once
by `UiService::TakeTextureChanges`, moved into the renderer's one
`UiTextureResidency` (`Queue(UiTextureChanges&&)`) that RendererServices names;
render/ui headers join the fingerprint because a module receives those frames by
reference. A surface names the platform window it lives on
(`SetSurfaceWindow`/`GetSurfaceWindow`), its frames carry that window, capture
can be asked per window (`Capture(window)`), and UiRenderFeature draws one
window's surfaces; the engine offers every window's events to the UI. A surface
names the layer it draws in (`SetSurfaceLayer`/`GetSurfaceLayer`) and its frames
carry it.

**Host images.** UiTextureRef gains the Host kind: a document names `host:NAME`
(`kUiHostImageScheme`), the host registers it with
`UiService::SetHostImage`/`RemoveHostImage` and publishes the pixels through
`UiTextureResidency::PublishHostImage`.

**Rows.** UiRow gains Key, Depth, Icon, Selected, Expanded, Mixed, Disabled,
Parts and Verbs (`std::vector<UiRowVerb>`; `UiRowVerb` is Key, Label, Removes).
`UiRow::Control` is a control's name, a `std::string`, rather than an enum: the
runtime names the three its own pages offer (`UiRowControls::Text`, `Range`,
`Choice`) and binds any other as written, so a host adds a control in its own
documents without growing this header.

**UiService measurement, scrolling and input.** `MeasureElement` takes the box
area to measure (`UiBoxArea`, Content by default); `MeasureScroll` and
`ScrollIntoView` are new; `EndPointerGesture(window)` ends the UI's side of a
pointer gesture. `ui/UiKeys.h` is new: `UiKey`, `UiKeyFromEvent` and
`UiKeyCodeFromEvent` read back the keys a document reports.

**Pointer gestures.** `PlatformEventRouter::AddConsumer` takes a third,
defaulted `GestureEnded` callback, and the router gains
`HandGestureToApplication`, `ReleasesMissedBefore`, `HasGesture` and
`ApplicationHasGesture`; its header now includes SDL_events.h.
`WindowFrameProbe` gains `PointerHeld`.

**Component metadata.** `IComponentSerializer` gains the `Placement()` virtual,
declared before the destructor, so the destructor's vtable slots moved.
`ComponentPlacementFacts`/`ComponentPlacement<T>` are new
(core/metadata/ComponentPlacement.h), as are `SENCHA_PLACEABLE` and
`SENCHA_PLACE_ICON`. `RuntimeField` gains `DisplayAngles`.

**Asset references.** AssetRef carries the asset's Id beside its path, and
AssetRef, AssetId and StrongId join the fingerprint, since a reference reaches a
module inside authored values.

**Asset kinds a module registers.** `IAssetStager` has one virtual,
`LoadStaged(record, source, const AssetPathsById&)`: every staging is handed
where assets named by id are now (`LoadStagedResolving` is gone).
`AssetKindRegistration::Commit` returns `AssetCommitOutcome` (the lease, or the
dependency that refused it), as does `AssetSystem::Commit`.
`AssetSystem::RetryStandIns` takes the async task queue, and `StageReload` and
`PathsById` are new. `RegisterAssetKind` takes a stand-in only for a loader that
can reload.

## v26

**A body can be assigned as well as asked for.** `Engine::AssignParticipantBody`
binds an entity the game already has, through the same validation and session
projection as `RequestParticipantBody`, and never destroys a refused entity.
`ParticipantBodyStatus` gains `InvalidBody` and `BodyInUse`; a `ProvideBody`
answer that is a participant or another participant's body is refused rather
than rebound. `ParticipantWithBody` names the participant holding a body.

**A spawned group can live as long as its root.** `SceneSpawnService::RequestSpawn`
takes a `SceneSpawnLifetime`; `OwnedByRoot` needs exactly one root record and
despawns the rest of the group at the pump after its root dies. `Root(id)` names
the single root a group was published with.

`app/BodySpawns` is replaced by `participant_spawn/ParticipantBodySpawns`. It
holds pending requests only, hands a landed body over through
`AssignParticipantBody`, and leaves group lifetime to `OwnedByRoot`, so games no
longer bind `ReapBody` to it. `participant/`, `participant_spawn/`,
`runtime/spawn/`, `world/scene/SceneInstanceGroup.h`, and the math headers
`Transform3f` is built from join the fingerprint.

## v27

**Spatial queries and perception.** `Engine` holds a `CandidateEvaluator`
(`TryCandidates`) and the perception query bindings, so its layout moved.
`PhysicsQueries::Raycast` gains a filtered overload taking `PhysicsQueryFilter`.
`spatial/candidates/`, `spatial/sight/`, `perception/` and `debug/DebugLines.h`
join the module-facing headers.

**Engine components.** `SightSensor` and `SightTarget` join the engine's
component set, after the animation components, which moves no existing
component's dense index or wire key.

**Entity order.** `EntityId` gains a defaulted three-way comparison (index, then
generation). Its layout is unchanged.

**Engine vocabulary.** `InstallEngineVocabulary(World&)` declares the engine's
verbs and the `perception.*` queries; a host that called `DeclareEngineVerbs`
alone sees no perception queries.

## v28

AbilityKit 2 (`docs/plans/abilitykit-2.md`); this entry gathers every stage of
that epic, which bumped the version once.

**Effects.** `ApplyEffect` takes the `FixedSimTime` it is applied on and an
optional `LeaseOwner`; `TickEffects` drops its delta (one call is one tick's
lifetime pass); `RemoveEffect` and `RemoveEffectsOwnedBy` are new.
`ActiveEffect` counts whole ticks and records its owner, application sequence and
granted tags. `CheckEffectCosts` (`effects/EffectCost.h`) is new.
`EffectRegistry::Register` refuses a definition granting more than
`kEffectGrantedTagCapacity` tags.

**Abilities.** `TryActivateAbility` and `ProcessAbilityActivations` take a
`FixedSimTime`.

**Physics queries.** `SweepShape` and `OverlapShape` gain filtered overloads
taking `PhysicsQueryFilter`, as `Raycast` did in v27.

**Input edges.** `InputFrame` logs button transitions in order
(`TransitionLog`, `SetButton`, `ClearTransitions`) in place of its six edge vectors;
`ConsumeKeyPressed` is gone. `InputActionFlags` gains `Interrupted`;
`InputActionView` and `InputActionTickRecord` carry the pass's ordered
`InputActionEdge` list. `InputEdgeLatch` is replaced by `InputMotionLatch` and
`InputEdgeAttribution`, and `AccumulateInputFrame` by `AttributeInputFrame`.
`NetCommandRecord` and `PawnCommandTick` carry edges; net protocol 7.

**Content vocabulary.** `DataFieldKind::StableKey` is appended (with
`IsValidStableKey`). `gameplay.attribute_declarations` and `effect.definition` are
engine data subtypes; `EffectRegistry::Define` is new.
`assets/runtime/ContentTagDeclarations.h` is now `ContentVocabulary.h`, adding
`DeclareContentVocabulary`, `RefreshContentVocabulary` and
`ReleaseContentVocabulary`.

**Catalogs.** `VerbDefinition` gains `Kind` (`VerbKind::Instant` or `Lease`),
`Scope` and `Prediction` (`authored/AuthoredClassification.h`), all part of the
contract. `VerbInvocation` carries `Owner` (`ecs/LeaseOwner.h`, which replaces
`EffectOwner`) and `Step`; `VerbInvocationSource` carries `Scope`.
`VerbAdmission` appends `ScopeMismatch` and `KindMismatch`, and `VerbTraceEvent`
appends `Released`. `VerbDispatcher::AcquireLease` and `ReleaseLease` are new.
`AnimEventScope` is now `AuthoredScope`, with the same values.
`InstallEngineVocabulary` also declares every engine domain's verbs and leases
and the physics queries.
`ApplyEffect` takes a scale, recorded on `ActiveEffect`. `AnimRequest::Owner` and
`AnimRequestDesc::Owner` are `LeaseOwner`s, which moves the replication table
hash (the record's local size; the wire record is unchanged). `Engine`'s members
changed.

**Phased abilities.** The single-shot kit is gone: `AbilitySet`,
`AbilityActivation`, `AbilityActivationQueue`, `AbilityActivationSystem`,
`AbilityAnimation`, `AbilityAnimationLeases`, `TryActivateAbility`,
`ProcessAbilityActivations` and `AbilityRegistry::Register`. Abilities are the
`ability.definition` data subtype (`AbilityDeclaration`, `CompileAbilityDeclaration`,
`ResolveAbilityDeclaration`, `AbilityDefinition`); `AbilityRegistry` names them by
asset path (`Reserve`, `Define`, `Undefine`, `Revision`, `Generation`). New
components `AbilityGrants`, `ActiveAbilities`, `AbilityLocals` (kit),
`ActorTime` (time) and `AimOrigin` (controller); new resources
`AbilityGrantEntries`, `AbilityRequestQueue`, `AbilityTrace` and
`InputActionVocabulary`. `AbilitySystem` and `ActorTimeSystem` replace the
activation system, and `RegisterAbilityKitSystems` takes an `AbilityHost` and is
called by the engine for every host, not by games. `ability.activate`,
`ability.cancel`, `ability.grant` and `ability.revoke` join the engine vocabulary.
`VerbTraceRing` is built on `core/containers/BoundedRing.h`.
`CompileAuthoredConstant` and `ParseVerbBindingArgument` are public. Scenes name `AbilityGrants` (chunk `ABGR`) where they named
`AbilitySet`. `EffectDeclaration` gains `GrantedAbilities`; `AbilityEffectGrants`,
`DeclareEffectGrants`, `ParseAbilityGrantEntries` and `AbilityGrantEntriesSchema`
are new.

**Actor time.** `ActorTimeLeases` (time) with `AcquireActorTimeLease`,
`ReleaseActorTimeLeases`, `PruneActorTimeHistory`, `ActorScaleAt` and
`ActorScaleLimits`; `ActorTicksAdvanced`, `ActorTicksSince`; `ActorTimeSystem` takes a
console and `AdvanceActorTime` a tick. The `time.scale` lease joins the engine
vocabulary. `EffectDefinition` and `ActiveEffect` gain a `TimeDomain`, and
`EffectDefinition` a `TimeScale`. `ActorTimeDilated` (time) is new, `CharacterMovement` derives
`ActorTime`, and `StepScaledCharacterTick` and `DilatedCharacterStepSystem` are new;
the bulk movement queries and the character drive exclude dilated characters.
`AnimPlayback` gains `TimeScale`, `RetimedTick` and `RetimedSeconds`, with
`RetimeAnimPlayback` and `RestartAnimPlayback`; `ActorScaleOnTick` and
`ActorScaleValue` (time) are new; a root-motion sample's velocity is per second of
actor time. `AuthoredPredictionName`, `EffectiveAbilityBinding` and `abilities/AbilityValueNames.h`
(the port, local-type and hit-field names) are new. `EngineVocabularyBindings` (`app/EngineVocabularyBindings.h`) is new and holds
the engine's domain operations and their bindings, the perception query bindings
among them, in place of `Engine`'s own members;
`AbilityEndReasonName` and `AbilityRefusalName` are defined.

**Held motion.** `movement/components/HeldMotion.h` (`MotionBursts`, `TetherSession`,
`LocomotionModeLease`) joins `CharacterMovement`'s derived set, so every character's
archetype gains them; `LocomotionModeId` moved to `movement/LocomotionModeId.h`.
`StepHeldMotion`, `HeldMotionSystem` and the `movement.burst`, `movement.tether` and
`movement.mode` leases (`MovementVerbs.h`) are new, and the replay kernel resumes the
burst and tether slots.

**Ability presentation.** `AbilityCueLog` (`sencha.ability_cue_log`) and
`AbilityPresentationState` (`sencha.ability_presentation_state`) join the
`AbilityGrants` derived set; `AbilityRecord` gains `LastPoint`, `LastNormal` and
`LastTarget`, and `AbilityOp` gains `Pulsed`. The `cue.presentation` data type
(`CuePresentationData.h`), `CuePresentationLibrary`, `AbilityPresentationSystem` and
`AbilityHost::Presents` are new, as are the `audio.play` verb and `audio.loop` lease
(`audio/AudioVerbs.h`), which join the engine vocabulary.

**Presentation layers.** `AnimRigLayer` and `AnimBoundLayer` gain `Presentation`;
`AnimPresentationRequests` (`sencha.anim_presentation_requests`) joins
`AnimationComponents`, composed under the new `AnimRigPart::Presentation`;
`AnimLayerRequests` and the `anim.present` verb (`AnimRequestOperations::Present`)
are new; `CollectAnimEvents` takes the presentation set beside the simulation's.

**Completion and lost targets.** `VerbDefinition` gains `Completes` (part of the
contract), `LeaseStep` gains `Probe`, and `VerbDispatcher::LeaseHolds` is new;
`movement.burst` and `movement.tether` complete. `AbilityTransitionTrigger` gains
`Complete` and `AbilityConditionKind` gains `TargetLost`.

**Ability replication.** `ActiveAbilities` and `AbilityCueLog` replicate through
codecs (`ActiveAbilitiesWire.h`, `AbilityCueLogWire.h`, precisions in
`AbilityWirePrecision.h`), which moves the replication table hash.
`ActiveAbilities` derives `AbilityCueLog` and `AbilityPresentationState`.
`AbilityRegistry` gains `WireKey` and `FindByWireKey`; `Fnv1a32` and `NameWireKey`
join `core/hash/Fnv1a.h`, which the tag registry and `AnimStableKey` now use; and
`WireTag`/`LocalTag` (`gameplay_tags/GameplayTagWire.h`) are what codecs translate
tags with.

**Received impulses.** `AnimRequest` gains the local-only `Received` and
`Observed`; `NoteAnimImpulsesObserved` and `kAnimReceivedImpulseTicks` are new.

**Owner state and shared tags.** `GameplayTagContainer`, `AttributeSet`,
`AbilityGrants` and `ActorTimeLeases` replicate through codecs
(`GameplayTagContainerWire.h`, `AttributeSetWire.h`, `AbilityGrantsWire.h`,
`ActorTimeLeasesWire.h`), moving the replication table hash.
`GameplayTagRegistry::SetReplicatedToAll`/`IsReplicatedToAll`,
`AttributeRegistry::WireKey`/`FindByWireKey` and
`AbilityGrantEntries::FindByWireKey` are new; `GameplayTagDeclarations` gains
`ReplicatedToAll`, read from an optional `replicate_to_all` list.

**Activation identity.** `AbilityRecord` gains `Identity` and
`AbilityActivationIdentity` is new; lease owner keys, `AbilityCueEntry::Activation`
and `AbilityPresentationState`'s shown records use it instead of `Sequence`, and the
record's wire form no longer carries `Sequence`.

**Prediction journal.** `PredictionJournal<T>` (`prediction/PredictionJournal.h`)
holds a client's guesses until the authority decides them; `AnimRequestJournal`
keeps its guesses in one.

**Ability prediction.** `AbilityRecord` gains `Predicted`; `AbilityPredictedCues`
joins the kit's components and `ActiveAbilities`' derived set;
`AbilityPredictionJournal`, `AbilityLeaseOwner`, `ReleaseAbilityLeases`
(`AbilityLeases.h`), `IsAbilityRecordShadowed` and `AbilityCueOccurrence` are new;
`AbilityPresentationState` gains the guessed-cue memory and departed records; the
ability pass runs on a client for its predicted pawn.

**Review of the merged kit.** `ActiveAbilities`, `AbilityLocals` and
`AbilityPresentationState` hold `kAbilitySlotCapacity` records: the authority's
`kAbilityRecordCapacity`, then `kAbilityGuessCapacity` guess slots
(`AbilityCapacity.h`). `AbilityActivationIdentity` takes the grant's id, which the
record's wire form now carries; `AbilityCueEntry` gains `Ordinal`, which the cue
log's wire form carries and `AbilityCueOccurrence` hashes with the target. The
replication table hash moves. `AbilityGrantEntries` rebuilds when the tag registry
grows, and a binding whose context names an unregistered tag is unbound.
`AbilityRefusal` appends `PredictionFull`; `AbilityPredictionJournal::Track` and
`Full` are new, and `Note` only updates guesses the journal already tracks.

**Engine teardown.** `Engine` declares its `BackRouter` before the pause menu and
options page that lease consumers from it, so they hand their leases back to a
live router at shutdown; a windowed host with a pause menu aborted on exit
("pure virtual method called"). The member order is part of the layout.

## v29

A render feature batch commits whole or not at all.
`Renderer::CommitStagedFeatures` takes no failed-id list: a Setup that returns
false withdraws every feature of the batch already set up, last first, so false
means nothing staged in it is registered. `Renderer::AddFeature` takes a
`FeatureRegistration` in place of a `RenderFeatureScope`, and commits that one
feature alone, leaving anything another host staged untouched; a named feature
is checked against the registered ids and its dependencies as a staged one is.

## v30

Navigation agents (`docs/plans/navigation-agent.md`); this entry gathers every
stage of that plan, which bumps the version once.

**Movement.** `MotionRequest` gains `PlanarOverridden`, set by `ComposeMotion`
when an action, root motion or held motion replaced the locomotion mode's
planar velocity. A dilated character's step now writes its last composed
request back to the component.

**World.** `RuntimeWorld::ZoneOf` names the zone whose partition holds an
entity. `ParseEntityText` (`ecs/EntityText.h`) reads what `AppendEntityText`
writes.

**Navigation.** `kNavMaxTraversalKinds` names the per-zone cap on distinct
traversal kinds (64, unchanged).

**Navigation agents.** `NavAgent` (authored: build profile name and policy
asset), with derived `NavAgentRequest` and `NavAgentProgress`, joins
`NavigationComponents`. `RequestNavigation`, `CancelNavigation` and
`ReadNavigation` (`navigation/NavAgentRequests.h`) are the request API;
`navigation/NavRouteFollowing.h` holds the path-following geometry.
`NavAgentSystem` (`navigation/NavAgentSystem.h`), registered by
`RegisterNavigationAgents`, follows them; `NavPlanQueue` and `NavRoutePlanner`
(`navigation/NavRoutePlanning.h`) are its queue and planning batch.
`navigation/NavTraversal.h` adds the traversal executor table
(`NavTraversalExecutors`, `RegisterNavTraversal`) and the engine's walking and
jumping executors.
`navigation/NavigationVerbs.h` declares the `navigation.move_to`,
`navigation.approach` and `navigation.stop` verbs and the `navigation.phase`,
`navigation.failure` and `navigation.remaining` queries; `EngineVocabularyBindings`
gains `BindNavigation`. `NavPlanarDirection` joins the path-following geometry,
and the navigation enums gain `EnumSchema`s.

## v31

Triggers, since generalized into responses in v32 (`docs/gameplay/responses.md`).

**Physics.** `CollisionShape` gains `Center` and `Orientation`, its primitive's
pose in its body's frame, and an equality operator. `Collider` gains a scene
schema (`collider`, chunk `COLL`); its cooked-mesh handle is not persisted.
`physics/CollisionShapeSchema.h` holds the shape's schema.

**Serialization.** `IWriteArchive` and `IReadArchive` gain `Subtree`, a value
whose shape no schema fixes, written whole; new vtable slots.

**Metadata.** `EditorVisual` gains `Kind::VolumeTint` and its `Fill` and `Edge`
colours.

**Logic.** `Trigger` (`logic/Trigger.h`) joins `LogicComponents`, declared in
`logic/LogicRegistration.h` (formerly `VerbRelayRegistration.h`), with its
sequence store (`logic/TriggerSequences.h`, `logic/VerbSequence*.h`),
`TriggerSystem` and the `trigger.*` verbs.

**Movement.** `movement.launch` joins the engine vocabulary.
`StepScaledCharacterTick` takes the tick's `CharacterTickContributions`.

**Gameplay tags.** `GameplayTagQueryNames` and `ResolveTagQuery` move to
`gameplay_tags/GameplayTagQueryNames.h`; `AbilityTagQueryDecl` names it.
`EntityMatches` (`gameplay_tags/EntityMatch.h`) is the entity condition
abilities and sequences share.

**Authored.** `MatchAuthoredArguments` (`authored/AuthoredArgumentMatch.h`)
pairs a call's authored arguments with the ones declared; ability, candidate
and sequence query calls compile through it. `DataEditorHint::Widget` gains
`"direction"`.

## v32

Responses (`docs/gameplay/responses.md`): an entity's authored answers to the
occurrences delivered at it, generalized from v31's triggers.

**Logic.** `Trigger`, `TriggerActivation`, `TriggerCooldownScope`,
`TriggerSystem`, `TriggerSequences`, `TriggerSerializer` and
`TriggerOperations` are removed. `Responses` (`logic/Responses.h`, scene key
`responses`, chunk `RSPN`) takes `Trigger`'s place in `LogicComponents`: a
content key into the World's `ResponseDefinitions` (`logic/ResponseSet.h`) and
two state masks. `ResponsesSystem` (`logic/ResponsesSystem.h`) replaces
`TriggerSystem`, with `Deliver<E>(subject, occurrence)` for native producers
and `logic.responses.*` cvars in place of `logic.trigger.*`.
`ResponseOperations` (`logic/ResponseVerbs.h`) declares `responses.enable` and
`responses.disable` in place of `trigger.enable` and `trigger.disable`, and
keeps `trigger.interact`; `EngineVocabularyBindings::Triggers` becomes
`Responses`. `TriggerInteractions` becomes `SensorInteractions`, whose requests
carry the tick they were made in.

**Sequences.** `SequencePort` carries the `DataFieldKind` and vector length of
the field it reads in place of `SequencePortKind`, which is removed.
`SequenceConditionKind` gains `Tag`. `VerbSequenceStore::Intern` takes the event
whose payload a sequence reads; `Revalidate` reads compile stamps once per pass,
which now include the event catalog; `PortsFor` builds a sequence's ports.

**Authored.** `AuthoredEventDefinition` gains `Instigator`, which takes part in
the event's contract. The engine declares the `sensor.*` events.

**Gameplay tags.** `GameplayTagQuery::Matches` gains a single-tag overload.

**Zone state.** `ZoneStateStore` gains per-entity component state records.

**Physics.** A body's shape takes its entity's scale when the body is built
(`ScaledCollisionShape`, `physics/CollisionShape.h`): an authored collider is
in its entity's frame, as its mesh is. A trigger volume's cooked box is fitted
unscaled accordingly.

## v33

Reversible mesh overrides (`docs/renderer/mesh-overrides.md`) and the owner
identity cosmetic changes are held by.

**Components.** `MeshOverrides` (`render/MeshOverrides.h`, runtime only) joins
`RenderComponents`. Every generated component companion now includes
`math/MathSchemas.h`, so a component's math members flatten the same in every
translation unit; the generator's output changed, which moves the fingerprint.

**ECS.** `ChunkView::TryRead<T>()` reads a column a query does not filter on,
empty where the chunk lacks it.

**Presentation.** `PresentationWriterId`, `PresentationOwner`,
`PresentationOwnerPrecedes`, `PresentationWriters` and
`RegisterPresentationWriter` (`presentation/PresentationOwner.h`) are new.

**Render.** `SetMeshOverride`, `ClearMeshOverride`, `ComposeMeshOverrides`,
`ApplyMeshOverrides`, `MeshOverrideContributions` and `MeshOverrideSystem` are
new; `ResolveSectionSlot` (`render/SectionMaterial.h`) is the slot rule
`ResolveSectionMaterial` now uses.
