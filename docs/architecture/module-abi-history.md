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

Projectiles (`docs/gameplay/projectiles.md`): launched prefabs the authority flies
and every machine draws.

**Projectile.** New `ProjectileComponents`, registered with the engine's:
`Projectile` (`projectile/Projectile.h`, scene key `Projectile`, chunk `PROJ`),
authored on a prefab root;
`ProjectileFlight` (`projectile/ProjectileState.h`), replicated through a codec
for its instigator; and the authority-only `ProjectileMotion`, whose lifetime
keeps the `ProjectileCount` resource. `ProjectileSystem`
(`projectile/ProjectileSystem.h`) is registered by the engine after the game's
systems when the game registered physics, ordered after `AbilitySystem` and
before `AttributeResolveSystem`. The engine declares `projectile.launch`
(`projectile/ProjectileVerbs.h`) and the `projectile.hit` event
(`projectile/ProjectileHit.h`).

**Physics.** `PhysicsWorld::GetGravity`.

**Net.** `ReplicationInterpolation::PresentedAuthorityTick`, the authority tick a
mirrored entity is drawn at, which `ReplicationInterpolationSystem` now reads.
`NetPrefabSpawner` no longer takes the `RuntimeWorld` it never used: it builds
into whichever World `Instantiate` is handed, as its schema describes it.

## v34

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

## v35

Attachments (`docs/gameplay/attachments.md`): entities worn at named rig
sockets or at an entity's origin.

**Components.** `AttachmentSet` and `Attachment` (`attach/AttachmentComponents.h`,
runtime only) are a new `AttachmentComponents` set; `PresentedWith`
(`render/PresentedWith.h`) joins `RenderComponents`.

**Attach.** `AttachToSocket`, `AttachToOrigin`, `Detach`, `DestroyAttachment`,
`DestroyAttachmentsOf`, `CreateMeshAttachment`, `UpdateMeshAttachment`,
`FindAttachment`, `ForEachAttachment`, `InstallAttachments`,
`AttachmentsInstalled` and `PlaceAttachments` (`attach/Attachment.h`) are new.

**Registration.** `SystemRegisterContext` gains `Presents`.

**ECS, assets and math.** `World::ReplaceComponent<T>` (the new value's `OnAdd`
before the old value's `OnRemove`), `AssetStoreTable::FindList`,
`Transform3d::FromMat4` and `Quat::FromPitchYawRollDegrees` are new;
`CameraRenderData::Excludes` is the camera exclusion rule extraction applies.

## v36

`presentation.attach` (`attach/AttachmentVerbs.h`): a cosmetic lease holding a
static mesh at a socket of its target, so an ability phase can show a prop from
content alone. `EngineVocabularyBindings` gains the `AttachmentOperations`
provider. `kAbilityMaxPhasePresents` and `kAbilityMaxSustainedPresentations`
(`abilities/AbilityCapacity.h`) bound what a sustained presentation's lease key
can name: an ability compile refuses a phase with more presents, and a cue
presentation declaration reports a cue sustaining more.

## v37

Camera presentation effects (`docs/gameplay/camera.md`, "Camera shake and
field-of-view scale").

**Components.** `CameraSubject` (`camera/CameraSubject.h`, runtime only) joins
`CameraComponents`: whose view a camera presents. `SetCameraSubject` sets it.

**Camera.** `camera.shake` profiles (`CameraShakeProfile`,
`CompileCameraShakeProfile`, `RegisterCameraShakeData`), the World resources
`CameraShakeState` and `CameraFovScaleState`, the shared `CameraEnvelope`,
`CameraEffectClock`, `CameraEffectTiming`, `CameraViewSubject` and
`CameraViewIsOwnedBy`, and `PresentCameraEffects`, which render extraction runs
for the camera it draws. The engine declares `camera.shake`,
`camera.shake.hold`, `camera.fov_scale` and `camera.fov_scale.hold`.

**App.** `EngineVocabularyBindings` holds the camera operations.

**Authored.** `MakeCosmeticVerb` (`authored/VerbRegistry.h`) and
`VerbDispatcher::GetDataAssets`.

**Core.** `SplitMix64` (`core/hash/SplitMix64.h`).

## v38

A targeted Use (`docs/gameplay/responses.md`, "Use").

**Logic.** `logic/InteractUse.h`: the `interact.use` verb and occurrence
(`InteractUse`, `InteractUseOperations`, `DeclareInteractUseVocabulary`) and the
read-only `FindUseCandidate`. `ResponsesSystem::Availability`, with
`ResponseReadiness` and `ResponseAvailability`, answers whether a subject's
responses would answer an occurrence without delivering it. `trigger.interact`
and `sensor.interact` are unchanged.

**App.** `EngineVocabularyBindings::BindInteraction` holds the use operations.

## v39

The frame's presented view, published once and shared by overlays and render
extraction (`docs/gameplay/camera.md`, "The presented frame").

**App.** A `PresentationUpdate(PresentationUpdateContext&)` schedule phase
(`EngineSchedule::RunPresentationUpdate`) runs after every FrameUpdate system
and before UI layout, with the published `PresentedFrame` in its context.
`RunPresentationPhase` (`app/PresentationPhase.h`) is that step: it publishes
the frame, then runs the phase. The fingerprint now covers
`camera/PresentedFrame.h`, `camera/PresentedView.h`,
`render/CameraProjection.h` and `time/FrameClock.h`, which reach a module
through the frame contexts.

**Camera.** `camera/PresentedFrame.h`: the `PresentedFrame` snapshot,
`ResolvePresentedFrame` (read-only), `PublishPresentedFrame`,
`EnsurePresentedFrame`, `TryPresentedFrame` and `ProjectWorldPoint`;
`PresentedFrame::Publication` counts publications, so a frame resolved again
after overlays laid out against it shows.
`PresentCameraEffects` measures shake falloff from the camera's drawn pose.

**Render.** `ProjectWorldPointToViewport`, `ViewportRect`, `ViewportPlacement`
and `ViewportProjection` in `render/CameraProjection.h`.
`CameraRenderDataSystem::Build(const PresentedFrame&, ...)`; the active-camera
overload now resolves through it.

**World.** `PresentationPoseOf` composes an entity's local transform onto its
parent's drawn pose instead of returning its WorldTransform, so it is right
before presentation propagation runs as well as after it.

## v40

The presentation layer: an HDR scene target, a post-processing phase, and outlines.

**Frame.** `RenderPhase` gains `Post` and `Output` and renumbers `ApplicationUi` and
`DevelopmentOverlay`. `MainColor` features record into a per-presentation scene target rather
than the swapchain scope, so a host registering its own owes an `OutputRenderFeature(params)` for
that presentation; `DefaultRenderPipeline::AddMeshRenderFeature` registers one.
`DebugLineRenderFeature` records in `Output`. `FrameContext` gains `SceneColorBindless`,
`SceneExtent` and `ScenePost` (`ScenePostState`). `RenderTargetDesc` gains `Lifetime`,
`RenderTargetView` gains `ColorAccess`, and `GpuScope` gains `PhasePost` and `PhaseOutput`. New:
`FullscreenPass`, `OutputTransformPass`, `SceneScopeFormats`, `kSceneColorFormat`,
`RenderTargetAccess`, `RenderTargetLifetime`, `FormatNeedsShaderEncode` and
`VulkanQueueService::RequiresSecondQueue`.

**Tone mapping.** The scene target is `RGBA16F` holding linear scene-referred light, and exposure
and the shoulder move into `OutputTransformPass`. `RenderLightSet` loses `Exposure`, `TonemapKnee`
and `TonemapEnabled`, now `OutputTransformParams`, filled by `ApplyOutputTransformCVars`.
`SkyGradientParams` and `SkyPushConstants` lose their output fields, and `MeshViewUniforms` replaces
`StyleParams` and `TonemapEnabled` with `DiffuseWrap` and `MinAmbient`. A game's emissive values
render as before except on blended surfaces, which now blend in linear light before the transform.

**Post effects.** `Game::OnRegisterPostEffects(PostEffectCatalog&)` is a new trailing vtable slot,
and `Engine::PostEffects()` reaches the catalog. New: `PostEffectContributions` with `SetPostEffect`,
`ClearPostEffect` and `ClearPostEffectsOf`, `PostEffectFrame`, `PostEffectChain` and
`PostRenderFeature`. `presentation/PresentationContributions.h` is the store post effects, outlines
and mesh overrides share, so `MeshOverrideContributions::ByEntity` is now one. The SDK installs
`SenchaShaders.cmake` and `post_effect.glsli`, and sets `SENCHA_SHADER_INCLUDE_DIR`.

**Outlines.** New: `render/outline/OutlineContributions.h` (`RegisterOutlineStyle`, `SetOutline`,
`ClearOutline`, `ClearOutlinesOf`, `IsOutlined`, `OutlineFrame`, `ExtractOutlines`),
`OutlineRenderFeature`, `OutlineMaskTargets` and `MeshForwardPass::DrawOutlineMask`.
`MeshViewUniforms::DebugViewPad0` becomes `OutlineStyleSlot`, and `RenderExtractionSystem::Extract`
takes an optional `OutlineFrame`.

**Fingerprint.** `render/post/`, `render/outline/` and `presentation/` headers join the
fingerprint, since `Engine` holds the catalog by value ahead of members a module reaches through
inline accessors, and a module builds effect, outline and owner values. `post_effect.glsli` joins
too: a module compiles its push block into its own SPIR-V. A game includes
`render/outline/OutlineContributions.h` for the outline calls and `OutlineStyles.h` comes with it;
`OutlineFrame` and `ExtractOutlines` are in `OutlineFrame.h`, which only a renderer needs, and the
chain's per-frame list is `render/post/PostEffectFrame.h`.

`OutlineStyleDesc::WidthDp` is in density-independent pixels, scaled by the display at
extraction through the new `RenderExtractContext::PixelsPerDp`; `ExtractOutlines` takes that
scale. `PresentationLifetimeSystem` replaces `PostEffectLifetimeSystem`, and outline
extraction takes a `const World&`.

## v41

**Material shaders.** `Game::OnRegisterMaterialShaders(MaterialShaderCatalog&)` is a new trailing
vtable slot, and `Engine::MaterialShaders()` reaches the catalog, which `Engine` holds by value.
`PostEffectCatalog` derives from the new `render/ShaderCatalog.h`. `Material` gains `Shader` and
`ShaderParams`, and `MeshPushConstants` grows from 80 to 112 bytes with `Params`. The SDK installs
`mesh_view.glsli` and `mesh_material.glsli`, which join the fingerprint with both catalog headers.

**Time and blending.** `PresentationTime`, `PresentedFrame` and `CameraRenderData` gain the
presented moment (`SimulationSeconds`, `TimeSeconds`), which reaches `ViewPositionTime.w`.
Blended surfaces composite premultiplied, so their emission is no longer scaled by alpha.

**Bloom.** `PostEffectDesc` and `PostEffectType` gain `Kind`, and the engine registers
`sencha.bloom`. `ShaderCatalog` leaves the empty-shader check to each catalog, since a bloom
type has none.

**Presentation settings.** `LoadedLevel::Manifest()` reaches the loaded world's manifest, which
gains `Presentation`, as `GraphRecord` does. `PresentationSettingsSystem` joins the engine's
systems. Cooked scenes move to `.smap` version 2 for the optional `PRES` section, so every
level recooks; `WriteCookedScene` takes the scene's layer. See
[presentation-settings.md](../renderer/presentation-settings.md).

## v42

Mechanisms for enemies that sense, remember, choose and strike, for
naming where a strike lands, and for answering the effects that land.

**Perception.** `ObservationMemory` (component, `OBSM`) and
`ObservationMemorySystem`, which `RegisterPerception` now schedules after
`PerceptionSystem`. `HearingSensor` (component), `PerceptionStimuli` and the
`perception.stimulus` verb; `PerceptionSystem::LastStimuli` and `LastHeard`.
`PerceptionComponentRegistration.h` registers the two new components, and
`PerceptionVocabulary.h` gains the memory queries and `PerceptionSenses::Hearing`.

**Selection.** `logic/selection/`: `SelectionTableDesc`, `SelectionTable`,
`SelectionTableData` (`selection.table`) and `SelectionEvaluator`.
`math/SeededPick.h`, `authored/AuthoredQueryCall.h` and
`gameplay_tags/GameplayTagQueryNames.h` are shared by the table and candidates.

**Candidates and navigation.** `NavDistanceToEdge` and
`ZoneNavigation::DistanceToEdge` with `NavEdgeDistanceResult`, behind the new
`escape_routes` and `edge_distance` measures; `CandidateRun::CandidateFlood`.

**Physics.** Every `CharacterVirtual` carries an inner body on the
`CharacterBody` layer, which queries strike and nothing collides with;
`CharacterMoverConfig::Owner` names the entity it answers for.
`PhysicsQueryFilter::IncludeCharacters` (default true) and `RaycastShape`.
`HitRegion` (component) and `HitRegionResolver`.

**Projectiles.** `ProjectileHit::Region`, the sixth `projectile.hit` payload
value. `Projectile::HitScaleQuery` (`hit_scale_query`) scales a strike's
effects; `ProjectileSystem::Services::Queries` answers it. Characters are
struck through their bodies, so `SweepCharacters` is gone.

**Effects and reactions.** `ApplyEffect` takes a trailing `EffectSource`, and
`EffectDefinition` gains `Kinds`. `EffectApplicationLog` records what each
application changed. `effect.apply` gains optional `instigator` and `from`
arguments. `EffectReactions` (component, `EFRX`), `effect.reaction_profile`,
`ReactToEffect` and `EffectReactionSystem`, which every host registers.
`movement.impulse` and `MovementOperations::Impulse`.

**Groups.** `ObservationMemoryEntry::Facing`, `ObservationMemory::Prefer`, `ObservationSharing`
(component, `OBSS`) and `PerceptionSenses::Shared`. `ClaimBoard`,
`claims.hold`, `claims.count` and `claims.held`. The `crowding` candidate
measure and `CandidateRun::ProjectNav`.

**Worn lights.** `PointLightComponent::PacksFirst`, a runtime-only field outside
the scene chunk. `LightAttachmentSpec`, `CreateLightAttachment`,
`attach/AttachedLight.h` (`AttachedLightState`, a World resource on every
`RuntimeWorld`) and the `presentation.light` lease. `CameraEffectClock::At`.

**Storage.** `Archetype::RowsFitChunk`, and a row too large for a chunk is fatal
in every build. `AbilityGrants` and `ActiveAbilities` no longer derive
`AbilityPredictedCues`; `AbilitySystem` adds it to the actor a client predicts.
`EngineSchedule` can time its systems (`SetTimingEnabled`, `CollectTimings`) and
host work (`ScheduleFrameWorkScope`).

## v43

**Lines and tethers.** `ContactLine` gains `max_length`, `leash_slack`,
`rehit_seconds`, `slots` and `slot_spacing`, and `ContactLineState` what they need;
prefabs carrying one must be recooked. `ProjectileLaunch` gains `MaxDistance`.

**ECS chunk storage.** `Chunk::Data` is now a pointer to a block taken from a
`ChunkBlockPool`, null while the chunk is empty. `World(std::shared_ptr<ChunkBlockPool>)`
shares a pool between Worlds; `Archetype::Blocks` must be set before a standalone
archetype makes a chunk. A pool keeps at most the larger of 64 blocks and a quarter
of those in use.

**Pooled activations.** `ActiveAbilities::Records` became `Slots`, a handle each;
records and locals live in the `AbilityActivations` World resource. The
`AbilityLocals` component is gone, and `AbilityRecordLocals` stays. Read through
`AbilityActivations::Record`, `IsAbilityLive` or `AbilityRecordsOf`.
`IsAbilityRecordShadowed` takes the activations. `ReplicationWireContext::Receiving`
is the writable World where a snapshot is applied. The wire is unchanged; the
replication table hash moved with `ActiveAbilities`' local size.

**Staggered sight.** `perception.sight.scan_interval_ticks`, `SightStats::HeldScans`,
`PerceptionFrame::AddHeld` and `LastScanRows`, and `ObservationPreference`.

**Pose storage.** `AnimPoseState::Layers` moved to `AnimPosePool::Slot::LayerStates`,
sized to the rig. A layer's own pose is kept only with `AnimPosePool::RetainLayerPoses`.

**Ability requests.** `AbilityRequestQueue::Push` no longer refuses and returns
nothing; `kAbilityRequestCapacity` became `kAbilityDormantRequestCapacity`, which
bounds only requests waiting on a dormant actor. A request's `Origin` counts that
actor's requests in the pass, not its place in the queue.

**Contact lines.** `line/`: `ContactLine`, `ContactLineSpan`, `ContactLineState`,
`ContactLineSystem`, `line.extend`, `line.cut`, `line.anchored` and
`line.anchored_within`.

**Projectiles.** `Projectile::Ignores`; `ProjectileStops`, what ended this tick.

**Navigation.** `NavLink::Anchored` and `AnchorOffset`, carried through
`NavLinkRecord`, `NavLinkInfo` and `NavRouteStep`; `.snav` format 2, so every zone
recooks. `NavTraversalExecutor::Abort` takes the executor's memory, and
`NavTraversalFrame::Ability` names the policy's ability. `NavTraversalCost::Ability`,
`kNavCrossByAbility`, `CollectNavCapabilities`, `NavAnchoredRelease`,
`NavAbilityCrossings` and the `navigation.crossing_*` queries.

**Abilities and movement.** A hold's `scope`; `AbilityOp::ForActivation`.
`TetherSession::Arrives` and `movement.tether`'s `until`. `physics.clear_path`.

**Frame scratch and skinned poses.** `EngineGraphicsConfig` gains the scratch pool floors and the skinned pose budget,
and the default scratch slice is 3 MiB. `RendererServices::Health`,
`Renderer::GetLastFrameHealth` and `RenderFrameHealth`. `ScratchTag` gains the
outline tags. `SkinnedPoseFrameData` loses `Ready`, and `SkinnedPosePass::Setup`
takes its dispatch capacity.

## v44

**Decision agents.** A new component, `DecisionAgent`, holds what an entity choosing its own options tracks.
`DecisionAgents` decides for every one from a `DecisionProfile` that a game embeds in its own profile. The game
registers its inputs and slots by name and adds its own state through `DecisionHooks`. See
`docs/gameplay/decision-agent.md`.

**Considerations.** `SelectionConsiderationDesc`, `SelectionConsideration` and `CandidateCriterion` hold a
`ConsiderationRule` in place of their own mode, bounds, curve, weight and unmeasured score.
`SelectionConsiderationMode` and `CandidateCriterionMode` are both `ConsiderationMode`.

**Helpers a game had written for itself.** `Sees`, `PredictMotion`, `NavAgentQueryRequest` and `NavCheckReach`
(also `NavigationQuery::CheckReach`), `CollectGroupMembersWithin`, `DescribeSelectionTrace`,
`DescribeCandidateTrace` and `GameModuleRun`. Authored values: `AuthoredField<T>`, `ReadAuthoredArgument<T>`
and `DataField::OptionalNumber`.

**Shared machinery.** `SpareRetention` replaces the retention constants of `ChunkBlockPool` and
`AbilityActivations`. `presentation/HeldEffects.h` starts, releases and retires held lights, shakes and pulses.
Also new: `AuthoredNameResolver`, `AppendSourcedCue`, `PlaceSpawnedRoot`, `ProjectileStops::Find`, and
`ProjectileSystem::PrefabOf(AssetRef)`. Overlap queries return each entity once, in identity order.

## v45

**View motion.** New components: `StrideCycle` (opt-in, authored), `CameraBob`,
`CameraTilt`, `PresentationOffset`, `InheritsPresentationOffset` and
`AnimFactSubject`, which move every later component's dense index and wire key.
`ResolvedMovementTuning` gains `StrideLength`, `MovementTuningPatch` a
`stride_length`. `PresentedView` gains `Offset`. `CameraShakeOffset` is
`CameraViewOffset` (`camera/CameraViewOffset.h`), and `ApplyCameraShake` is
`ApplyCameraViewOffset`. `PropagationEntry` gains `ParentOffsetPtr`.

**Animation.** Fact slots `from: subject`; the `rate` and `spring` derivations,
with `AnimDerivedFactDecl::Damping`; `AnimDerivationMemory` reorders to hold a
spring's velocity at the same size. A behavior's `phase_fact` and
`FollowAnimPlaybackPhase`. A layer's additive `reference`. `BindLookAnimFacts`,
and movement's `StrideCycle` and `StrideSpeed` facts.

**Sockets.** `PresentedSocketPose`.

## v46

**Level change.** `Engine::LevelChanges()` returns the new `LevelChange`
(`app/LevelChange.h`), which replaces what is loaded as one deferred request
with observable outcomes. `LoadedLevel` gains `Loaded()` (a `LevelReference`,
`app/LevelReference.h`) and `Residency()`, and keeps its collision connection
across `Unload`. `ConsoleService::NoteLoadedMap` and `CurrentMap` are gone:
the loaded level is the one record of what is loaded.

**Participants.** `ParticipantLifecycle::ReleaseBody`, the session
projection's `ReleaseBody` and `Engine::ReleaseParticipantBody` let a body go
and keep its participant; `Retire` is that release followed by destroying the
participant.

**Conditions and bounds.** `ValueBounds` (`math/ValueBounds.h`) is the
authored `require` block. `ConsiderationRule::Min` and `Max` are its
`Bounds`. `SequenceConditionDesc::Expect` and `CompiledSequenceCondition::Expect`
are `Require`, and a query condition may bound a number.
`AuthoredValue::TryGetNumber`.

**Files.** `WriteFileBytes` replaces a file in one step; `WriteFileText`.

## v47

**Saves.** `Engine::Saves()` returns `SavedGames` (`save/SavedGames.h`):
write and restore requests, registered sections, the `save.write` and
`save.restore` verbs. `SavedGame`, `EncodeSavedGame` and `DecodeSavedGame`,
`SaveSlotFiles`. `EngineRuntimeConfig::SavesRoot`; `UserDataDirectory()`.
`LevelChangeRequest::ZoneState`. `LoadedLevel::CaptureState`.

**Zone state.** `ZoneStateLifetime` joins the world components after
`SceneInstance`, which moves every later component's dense index and wire key.
`ZoneStateStore` records each entity's lifetime and versioned component state,
captures live or at detach (`RecordCapture` replaces `RecordDetachCapture`),
and exports and installs its durable part. `ZoneComponentMemories` and
`CaptureZoneState` / `RestoreZoneState` (`zone/ZoneStateMemory.h`).
`ResponsesSystem` no longer captures or restores zone state itself.

**Files.** `HashedChunkFileWriter` and `ReadHashedChunkFile`
(`core/serialization/HashedChunkFile.h`); `SlugOf` (`core/text/Slug.h`).

## v48

**Spawn group identity.** `SmapPackageOptions::StripPersistentIdentity` is
`StripAuthoredIdentity` and also drops `scene_instance`, so every member of a
spawned prefab, placed members included, joins the spawn's group. A refused
group stamp fails the spawn.

**Grant entry wire identity.** `AbilityGrantEntries` names an entry on the wire
by the `NameWireKey` of its key and binding (`WireKey`), not of its key alone,
and `Intern` refuses a second entry of one ability with the same wire key
(`kAbilityGrantEntryRefusal`). `DeclareEffectGrants` returns the keys it could
not intern, and content declaration reports them. The grant codec's layout is
unchanged, so the replication table hash does not move; the word's meaning does,
so `kNetProtocolVersion` is 9. The ability pass revokes ended effect grants, and
grants whose entry a reload took off their effect, before it grants;
`AbilityTraceEvent` appends `GrantRefused`.

**Composition operator.** `core/composition/Composition.h` (`ComposeLayers`,
`ApplyCompositionPatch`, `CompositionReport`, `CompositionLogicalPath`) and
`JsonComposition.h`; `JsonEquals`. `DataFieldSchema` gains `MergeKey` and
`MergePolicy` (`DataMergePolicy`), which moves every later member;
`DataValueRules` gains `RequireMembers`. `ValidateDataAgainstSchema` takes a
`DataValidationMode`. `UnwrapDataField`, `DataFieldElement` and `ArrayMergeKey`.

**Composed data definitions.** `StageDataDefinition`, `DataDefinitionContext`,
`DataDefinitionStaging`, `DataSourceReader`, `IsAbstractDataDefinition` and
`SplitCompileError` (`assets/data/DataDefinitionStaging.h`); the data loader and
Kyusu both stage through it. A data envelope may name `extends`, `includes` and
`abstract`. `DataAssetLoader::StageRoot` and `RuntimeAssets::StageDataRoot` take
a reader; `CompiledDataAsset` and `DataAssetEntry` carry `Sources`;
`DataAssetCache::Register` and `ReloadInPlace` take them and `ComposedFrom`
answers the reverse. `AssetCommitResult` gains `Error`. A kind's `Reload`
returns `AssetReloadOutcome`, and `AssetKindRegistration` gains `ComposedFrom`,
which the hot reloader consults for every changed file. `PeekDataAssetEnvelope`.

**Fact schemas compose.** `animation.fact_schema` is subtype version 2:
`extends` moved from `data` to the envelope, and `slots` and `derived` are
append-only lists keyed by name. A version-1 file is refused with the note
`DataAssetTypeRegistration::Migration` carries, a new member; a game's version-1
schema that never extended anything only changes its version. `AnimFactSchema`
loses `Extends`; the binder's chain walk and `anim.fact.extends_cycle` and
`anim.fact.duplicate_slot` are gone, and a bound slot's `DeclaredAt` names it by
key. A rig's `sockets` compose by name. `DataAssetCache::GetCompositionSources`,
`AssetHotReloader::SetHostHeld`, `SourceReloadRoots::SetHeldFilter`.

**Scene composition schemas.** `IComponentSerializer` gains
`CompositionSchema()`, a new virtual with a default declared before the
destructor: a record schema saying how the component's scene form composes when
a prefab extends or includes another, or a placement patches it (which lists
merge by which member). `MakeKeyedListRecordSchema` builds the common shape.

**Grant source.** `AbilityGrant` renames `Effect` to `Holder` and gains
`Source`, which changes `AbilityGrants`' layout; `GrantAbility` takes the source
after the holder, `AbilityPort` gains `Source`, and `HasLiveActivationFrom`
joins `IsAbilityLive`. The grants wire sends the source in place of the holding
effect (`AbilityGrantsWireSources`, field `sources`): no bytes change, the
replication table hash does, and the protocol stays 9.

**Request selection.** `AbilityRequest` gains `Source`. An activation request
takes the grant with the highest effective priority, then the lowest grant id;
`AbilityEffectiveBinding::Priority` is resolved for an entry bound to no
action. `ability.activate` and `ability.grant` take an optional `source`.

**Late-bound references.** `ReplicationWireContext` gains `Unresolved`, and
`ReplicationClientIdentity` holds the `ReplicationDeferredReferences`
(`net/ReplicationDeferredReferences.h`) it translates again once an entity an
image named binds. No wire layout changes.

**Grant parameters.** `AbilityGrantDeclaration` and `AbilityGrantEntry` gain
`Parameters` (`AbilityGrantParameter`), which join an entry's content and its
wire key; `AbilityDeclaration` and `AbilityDefinition` gain `Parameters`
(`AbilityParameterDecl`, at most `kAbilityParameterCapacity`), `AbilityValue`
and `AbilityValueDecl` gain `Parameter`, and `AbilityValueSource` gains
`Parameter`. `AbilityGrantEntries::Parameters` answers an entry's resolved
values. Scene grants and effect grant entries read and write `parameters`; a
file without them loads as before.

**Override precedence.** `EffectModifier` and `EffectDeclaration::Modifier`
gain `Precedence`, authored `precedence` (default 0): of the overrides of one
attribute on one target the highest wins, and `effects/EffectSystem.h` declares
`EffectOverrideTies`, where a tie at the top is recorded once per attribute and
pair of definitions.

**Cost providers.** `abilities/AbilityCostProviders.h` declares the cost
provider catalog (`AbilityCostProviderCatalog`, its registration scope, and
`AbilityCostProviderDefinition` with `Check` and `Apply`, or `SpendsThrough` and
`Translate`), installed with the AbilityKit vocabulary. `AbilityCostGroupDecl`
and `AbilityCostGroup` gain `Target` (`AbilityCostTarget`) and `Resources`, and
`AbilityResolveEnvironment` gains `CostProviders`. `CheckEffectCosts` takes
`EffectCostRow`s (a target and an effect each) in place of one actor and a list
of effects, and `EffectCostCheck` gains `Target`.

**Grant reservations.** `effects/EffectSystem.h` declares
`EffectApplicationAdmission`, which `ApplyEffect` asks before it spawns a lasting
effect and tells once it has; `abilities/AbilityEffectGrants.h` declares
`AbilityGrantReservations`, `AbilityGrantRoom` and
`InstallAbilityGrantAdmission`, which `RegisterAbilityKit` installs. A granting
effect whose grants would not fit is refused at application, and
`ability.grant` leaves the room a reservation holds.

**Transient grants.** `AbilityGrantKind` gains `Transient`, and
`AbilityRequest` gains `Entry`, with the request kind `ActivateEntry`: a grant
that lives as long as the one activation it was made for. The grant codec sends
the kind as it always has.

**Named definition registry.** `core/identity/NamedDefinitionRegistry.h`
declares the registry of one World's definitions named by asset path, which
`AbilityRegistry` now is; its members and their behaviour are unchanged, and
`abilities/AbilityRegistry.cpp` is gone.

**Runtime persistent ids.** `world/identity/PersistentIdMint.h` declares
`PersistentIdMint`, `MintRuntimePersistentId` and `EnsurePersistentId`. The
snapshot writer sends a persistent id as authored only outside the runtime
namespace, and the package importer refuses a package entity holding a
runtime-namespace id. No wire layout changes.

**Persistable references.** `world/identity/PersistableRef.h` declares
`PersistableRef`, `MakePersistableRef`, `ResolvePersistableRef`,
`RefreshPersistableRef` and its scene codec. `SceneSerializationContext` gains
`PackageMembers` (`PackageMember`), `PackageEntity` gains `MemberId`
(`EntityBuildPackage::SetMemberId`), and `World` gains
`AssignEmptyEntitySignature`.

**Authority-materialized components.** `SENCHA_AUTHORITY_MATERIALIZED` is a new
component annotation: `ComponentDefinition` and `TypeSchema` may state
`AuthorityMaterialized` (`ComponentIsAuthorityMaterialized`),
`WorldComponentSchema::Entry` and `ReplicatedComponent` gain the flag, and
`zone/ZonePackageImporter.h` declares `IsAuthorityMaterialized`. The flag is not
part of the replication table hash.

**Per-entity relevance.** `NetRelevanceAnchor` is a new engine component in
`NetComponents` (`kNetRelevanceAnchorDepth`), and the change store's
`EntityState` gains `Anchored`, `AnchorPastBound` and `RelevantPeer`.
`ReplicationPeerState` gains `NoteSeeded`, and `NoteDeparted` voids what
pending snapshots said about the entity. The anchor does not travel, so the
replication table hash does not move.

**Baseline tag stacks.** `GameplayTagContainer` gains `Baseline`, which adds 64
bytes, and `GrantBaseline`, `RevokeBaseline`, `Erase`, `SetContribution`,
`BaselineCount` and `ContributedCount`; `Grant` and `Revoke` are contributions,
and a revoke never takes the baseline. The tag serializer writes the baseline
alone and `ReadGameplayTags` gains a `dropped` list. `GameplayTagRegistry` gains
`SetProjected` and `ProjectedBy`. `ModeTagProjectionSystem` is a new movement
system, and `movement/MovementTags.h` declares `kModeTagProjection` and
`kSupportTagProjection`. The container's local size moves the replication table
hash; its wire image is unchanged, and the protocol stays 9.

**Tag overflow.** `gameplay_tags/GameplayTagOverflow.h` declares
`GameplayTagOverflow`, `NoteGameplayTagOverflow` and
`ReportGameplayTagOverflow`; `GameplayTagContainer` gains `CanHold`. An
effect's granted tag or an ability's owned tag that a full container refuses
is counted, and `AbilitySystem` reports the count.

**Tag verbs.** `abilities/GameplayTagVerbs.h` declares the `tags.grant` and
`tags.revoke` verbs (`DeclareGameplayTagVerbs`, `GameplayTagOperations`),
declared with the engine vocabulary after the ability verbs, which moves every
later engine verb's place in the catalog. `EngineVocabularyBindings` gains
`Tags`.

## v49

**Serializers save from the World.** `IComponentSerializer::Save` takes
`const World&` in place of `const Registry&`, so a running World's entities can
be written; editor and cook callers pass `Registry::Components`.

**Bodies in saves.** `SceneSpawnService::RequestSpawn` takes a
`SceneSpawnOverlay` (`runtime/spawn/SceneSpawnOverlay.h`): a patch composed over
the scene's root before import and the persistent id the root gets once
published, with `CaptureSpawnOverlay` and `ApplySpawnOverlay`.
`RestoredBodies` (`participant_spawn/RestoredBodies.h`) holds the local
participant's next body, which `ParticipantBodySpawns` takes.
`RegisterEngineSaveSections` adds `sencha.bodies` and `sencha.persistent_ids`.
`SavedGameSectionDefinition::Capture` takes `World&`. `PatchFrom`
(`core/composition/CompositionPatch.h`) is the composition operator's inverse,
and Kyusu's `DiffSceneComponents` uses it. `EntityBuildPackage` gains
`FindComponent`, `SetSerializedComponent` and `RemoveComponent`. The ability
grant serializer saves grants `ability.grant` made.

## v50

**Held records.** New component `Held` (`world/held/Held.h`): a record entity's
holder, a `PersistableRef` where invalid is the world, and its position.
`HeldRecordIndex` (`world/held/HeldRecordIndex.h`) is a World resource kept by
`Held`'s hooks and by `PersistentIdComponent`'s; `HoldRecord`,
`SettleHeldRecords`, `DestroyHeldRecords` and `InstallHeldRecords`
(`world/held/HeldRecords.h`) are the World operations. `RuntimeWorld` installs
the index and a `PartitionFollowers` resource, and settles held records before
it dispatches partition moves. `RegisterEngineSaveSections` adds `sencha.held`
(`save/HeldRecordSaves.h`) between `sencha.bodies` and
`sencha.persistent_ids`.

**Counters.** New component `Counter` (`logic/Counter.h`) in
`LogicComponents`, with its serializer and codec. Verbs `counter.set` and
`counter.add`, query `counter.value`, event `counter.changed`
(`logic/CounterVocabulary.h`), bound by
`EngineVocabularyBindings::BindCounters`, whose `Entities` member is now a
mutable `World&`. `TypedComponentSerializer<T>`
(`world/serialization/TypedComponentSerializer.h`) holds what every
hand-written serializer did alike (identity, defaults, storage, `Load`,
`Remove`); the engine's seven and the counter's derive from it.

**Hooked components on the wire.** `WorldComponentSchema::SetComponentBytes`
overwrites a component with lifecycle hooks as `ReplaceComponent` does, firing
its hooks; `ReplicationLayout::AddCodec` accepts a hooked component, while
`Add` (bytes on the wire) still refuses one. The replicated table gains `Held`
and `Counter`, which changes its hash; no wire layout of an existing component
changed, so the protocol stays at 9.

## v51

**Inventory.** New components `ItemEntry`, `ItemContainer` and `ItemPickup`
(`inventory/InventoryComponents.h`) in `InventoryComponents`, registered after
`LogicComponents`, with their serializers and `ItemEntry`'s codec. An entry's
container and position are its `Held`. `ItemRegistry` (`inventory/ItemDefinition.h`)
and the `item.definition` data type; `Inventory` operations and reads
(`inventory/Inventory.h`); `InventorySystem`; the `inventory.item` cost
provider; verbs `inventory.take`, `inventory.give`, `inventory.transfer`,
`inventory.drop`, `inventory.use` and query `inventory.count`, bound by
`EngineVocabularyBindings::BindInventory`. `InstallInventoryVocabulary` runs in
the engine host, Kyusu documents and the ability preview.

**Held records.** `NoteHeldRecordChanged`, `ParkHeldRecords` and
`DestroyAwaitingHeldRecords` join `world/held/HeldRecords.h`;
`HeldRecordIndex` gains `Await`, `Holders`, `AwaitingRecords`, `Depth` and
`Height`. `HoldRecord` to another holder, and a noted change, give a record
carrying an authored id a runtime one. `DestroyHeldRecords` leaves records
with authored ids. `CaptureHeldRecords` also keeps runtime records of authored
holders and records waiting for a holder. `HasAuthoredPersistentId`
(`world/identity/PersistentIdMint.h`).

**Composition.** `ComponentSerializer<T>::CompositionSchema` declares the
fields whose `SceneFieldCodec` names a `CompositionKind`; `PersistableRef`'s
names `Entity`, so `Held`'s holder is renamed by a placement.
`WriteSceneDefinitionRef` and `ReadSceneDefinitionRef`
(`world/serialization/SceneAssetRef.h`).

**Names.** `NamedDefinitionRegistry::Reserve` refuses a name whose wire key
another name holds, and `WireKeyHolder` names that other; content declaration
reports the refusal for abilities and items.

The replicated table gains `ItemEntry`, which changes its hash; no wire layout
of an existing component changed, so the protocol stays at 9.

## v52

**Equipment.** New component `EquipmentState` (`equipment/EquipmentComponents.h`)
in `EquipmentComponents`, registered after `InventoryComponents`, with its
serializer and codec (`equipment/EquipmentStateWire.h`).
`EquipmentLayoutRegistry` and the `equipment.layout` data type
(`equipment/EquipmentLayout.h`, `EquipmentLayoutData.h`); `Equipment`
operations, `ResolveEquipmentSlots`, the locks, `ReconcileEquipment` and
`RedefineEquipmentLayout` (`equipment/Equipment.h`); `EquipmentSystem` and
`InstallEquipmentMoveAdmission`; `DeclareWornGrantEntries`
(`equipment/WornGrantEntries.h`), which content declaration calls; verbs
`equipment.equip`, `equipment.unequip`, `equipment.cycle`, queries
`equipment.in_slot` and `equipment.sources`, bound by
`EngineVocabularyBindings::BindEquipment`, and the `equipment.slot` cost
provider. `InstallEquipmentVocabulary` runs in the engine host, Kyusu
documents, the vocabulary catalog and the ability preview.

**Inventory.** `ItemDefinition` gains `Occupies`, `EquipEffects`,
`EquipLockTags` and `GrantParameters`, and `ItemDeclaration` their authored
forms. `FindItemDefinition` joins `inventory/Inventory.h`.

**Abilities and effects.** `AbilityEffectGrants` gains per-instance entries
(`SetInstance`, `Of(instance, effect)`, `ForgetEnded`, `InstanceCount`) and
`Generation`; `AbilityGrantReservations::HeldFor` and `AbilityGrantRoom` take
effects counted as already ended. `AbilityGrantEntries` gains `IndexOf` and an
index by ability and wire key. `EffectRegistry` gains `Revision` and
`Generation`, `ActiveEffect` gains `Revision`, and `effects/EffectSystem.h`
gains `FindEffectsOwnedBy` and `IsEffectInstanceCurrent`.
`ParseAbilityGrantParameters` and `AbilityGrantParametersSchema`
(`abilities/AbilityDefinitionData.h`).

**Shared helpers.** `MakeOptionalDataField` (`core/metadata/DataSchema.h`),
`MakeGameplayVerb` and `MakeEntityArgument` (`authored/VerbRegistry.h`), and
`FixedSimulationLoop::TimeOfTick`, which every clocked verb now uses.

**Snapshots span datagrams.** The unreliable channel fragments a message
larger than one datagram (`kNetPacketFragmented`,
`kNetMaxUnreliableMessageBytes`, `NetChannelSet::PendingUnreliableReassemblies`);
`kNetMaxSnapshotBytes` is the largest such message and
`kNetDefaultSnapshotBytes` (four datagrams) the default budget. An ownership
move no longer owes an owner-only run still at the default it was added with.
The packet framing changed, so `kNetProtocolVersion` is 10. The replicated
table gains `EquipmentState`, and its frozen hash moves
(`test/core/ComponentIdentityFreezeTests.cpp`).

## v53

**Row lists.** `ListPool` (`core/containers/ListPool.h`, `docs/ecs/row-lists.md`):
lists a component row names by handle, referenced through the component's
lifecycle hooks.

**Grants.** `AbilityGrants` no longer holds its grants: it is a handle into the
World's `AbilityGrantLists`, 16-bit counts of all, effect-held and transient
grants, and the next id, and declares
`OnAdd` and `OnRemove`. `AbilityGrants::Find` and the member array are gone;
read with `AbilityGrantsOf` and `FindAbilityGrant`. `GrantAbility` and
`RevokeAbilityGrant` take the World and the actor, or an `AbilityGrantList`
(the by-value form, new, with `CopyAbilityGrants`, `MakeAbilityGrants` and
`SetAbilityGrants`). `kAbilityGrantCapacity` is 64. `AbilityGrantsWire` is
sixteen pages of four grants (`kAbilityGrantsPerWirePage`,
`kAbilityGrantWirePages`).

**Equipment.** `EquipmentState` no longer holds its records: it is the layout,
a count, the next key and a handle into the World's `EquipmentRecordLists`, and
declares `OnAdd` and `OnRemove`. Read with `EquipmentRecordsOf`; write with
`EditEquipmentRecords`, `AddEquipmentRecord`, `RemoveEquipmentRecord`, or by
value through `EquipmentRecordList`, `CopyEquipment`, `MakeEquipmentState` and
`SetEquipment`. `kEquipmentRecordCapacity` is `kMaxEquipmentSlots` (32).
`EquipmentRefusal::Full` is removed, which renumbers the refusals after it.
`ResolveEquipmentSlots` takes the worn records as a span. `EquipmentStateWire`
is 32 record runs and 16 runs of two sources; `EquipmentWireRecord` is gone.

**Wire.** Both images changed, so `kNetProtocolVersion` is 11 and the frozen
replication hash moves (`test/core/ComponentIdentityFreezeTests.cpp`).

A module that assigns over either component's row, or copies the row to keep
its value, must use the calls above instead (`docs/ecs/row-lists.md`, "What may
not be done with such a component").

## v54

**Restore progress.** `SavedGames::RestoreStatus()` is a `RestoreProgress`
(`Pending`, `Reading`, `Loading`, `BodiesRestored`, `Failed`) that follows the
restore's own level change to the participants' bodies; `Restores()` counts
accepted restores. `RestoreChange()` and `Failure()` are gone; `WriteFailure()`
and `RestoreFailure()` replace the shared reason. `RestoredFromBackup()`,
`Finish()` and `Advance()` are new. `SavedGames` writes the persistent id
counter itself (`kPersistentIdsSection`), and `RegisterEngineSaveSections` no
longer registers it; every registered section's `Restore` runs on every
restore, with null data when the save lacks it.

**Level change.** `LevelChangeRequest::RuntimeState`
(`LevelChangeRuntimeState::Keep` or `Discard`) and `LevelChange::RuntimeState()`;
`LevelChangeAdmissionReason`. A first load is admitted on any machine, and the
`map` and `world` commands are level change requests. `RestoredBodies::Refusal`.

**Files and formats.** `DecodeSavedGame` returns a `DecodedSavedGame` (the
save, its error, and whether a newer build wrote it); `ReadHashedChunkFile`
returns a `HashedChunkFileRead`. `SaveSlotFiles::Write` no longer rotates;
`WriteBackup` is new. `ApplySpawnOverlay` takes the list of what a retuned
scene set aside. The save file format is unchanged (format 1, chunk versions 1).

## v55

**Tag writes.** `GameplayTagContainer::Grant`, `GrantBaseline` and
`SetContribution` return a `GameplayTagWrite` (`Unchanged`, `Added`,
`Restacked`, `Removed`, `Full`) in place of a bool, and never assert; a full
container answers `Full` and changes nothing. `CanHoldAll` replaces `CanHold`. A baseline
stack added at saturation no longer displaces a contributed one.
`gameplay_tags/GameplayTagOverflow.h` gains `AdmitGameplayTags`,
`kGameplayTagOverflowQuietReports` and `GameplayTagOverflow::QuietReports`;
`gameplay_tags/BaselineGameplayTags.h` declares `GrantBaselineTag` and
`RevokeBaselineTag`. The tag verbs moved from `abilities/GameplayTagVerbs.h`
to `gameplay_tags/GameplayTagVerbs.h`. `ReadGameplayTags` reports dropped tags
as `DroppedGameplayTag` (name and `GameplayTagDrop`), and `ReadAttributes`
takes an optional list of dropped attributes. `ApplyEffect` refuses a lasting
effect whose granted tags would not fit, and `AbilityRefusal::TagsFull` is
appended.

**Grants.** `AbilityGrant::Source` is a `PersistableRef`, and `GrantAbility`
takes the source as one. `AbilityGrantEntries::Intern` takes an optional
`AbilityGrantEntryRefusal*`; `kAbilityGrantEntryRefusal` is replaced by
`AbilityGrantEntryRefusalText`, and `DeclareEffectGrants` returns a message
per refused grant instead of its key.

**References.** `MakePersistableRef` takes a mutable World and mints the
target a persistent id on the authority; `PersistableRefOf` is the const,
never-minting form.

The grants serializer writes `"kind": "request"` and `"source"` on a requested
grant; both are optional, so older scenes and saves load as before. The wire
and the save format version are unchanged.

## v56

**Inventory.** `Inventory` takes the tick it runs in (`Inventory(world, now)`),
as `Equipment` does; a counted pickup's take effects are applied in that tick,
scaled by its count. `InventoryOperations` and `EquipmentOperations` take the
simulation clock and a `Logger`, and `EngineVocabularyBindings::BindInventory`
and `BindEquipment` take the logger. `InventoryRefusal` and `EquipmentRefusal`
gain an `EnumSchema`. `inventory/Inventory.h` declares `ItemStackHolds` and
`SetItemCount`.

**Equipment.** `Equipment::UnequipRecord` ends a record by its key.
`EquipmentComponents.h` replaces `AddEquipmentRecord` with `WearRecord` and
`WearEquipmentRecord`, and declares `FindEquipmentRecordHolding` and
`kNoEquipmentRecord`; `Equipment.h` declares `ItemTakesSlotFirst`.

**Held records.** `HeldRecordIndex` keys records (`HeldRecordKey`, `Key`,
`Unkey`, `FindKeyed`), answers `IsWithin`, `PositionOf` and `Renumber`, and
loses `ForgetHolder`; a holder holding nothing has revision 0. `Counter` gains
lifecycle hooks and `CounterKey`. `world/held/HeldRecords.h` declares
`PlaceHeldRecord` and `NameHeldHolder`.

**Tags.** `GameplayTagContainer::CanHoldAll` answers no for a tag already at
0xFFFF stacks, so `AdmitGameplayTags` refuses it.

`NamedDefinitionRegistry` gains `ForEachName` and `ForEachDefined`. The wire,
the replicated layout and the save format version are unchanged; a scene or
save naming an entry count its stack cannot hold, or none, now fails to load.

## v57

`SavedGames::Remembers` answers whether this session wrote or restored a slot,
where `Holds` also answers yes for a file an earlier run left on disk.
`inventory/Inventory.h` declares `ItemStackClamp`, which `ItemStackHolds` is
defined by; a scene or save entry naming a count its stack cannot hold now
loads with the count clamped into 1 to `stack.max` and a warning, instead of
failing to load. The wire, the replicated layout and the save format version
are unchanged.

## v58

`IWriteArchive` and `IReadArchive` gain `Field(std::string_view, std::int32_t)`,
so a signed integer member has a scene form: JSON writes and reads the number
itself (a priority of -1 is written -1, not 4294967295), and the binary archive
writes the same four bytes it wrote before. `ReadArchiveValue` refuses a number
that is not whole or does not fit the member's type, where it used to truncate,
and an integer wider than 32 bits has no archive form. Cooked data, the wire,
the replicated layout and the save format version are unchanged.

## v59

`IComponentSerializer` gains the virtual `Persistence()`, returning the new
`ComponentPersistence` (`core/metadata/ComponentPersistence.h`: `Authored`,
`Saved`, `Identity`), declared before the destructor so every later slot
moves. `SENCHA_SAVED` is a new component annotation: `ComponentDefinition` may
state `Saved`, which `ComponentPersistenceOf<T>` projects and both
`ComponentSerializer<T>` and `TypedComponentSerializer<T>` return; a component
may instead specialize `ComponentPersistenceOf` by hand, as
`PersistentIdComponent` and `SceneInstance` do for `Identity`. The generator's
format version is unchanged, since the member is optional. A save's body patch
now holds only `Saved` components, a held record leaves out `Identity` ones,
and a spawn strips `Identity` ones from a prefab, each read from the
declaration. Cooked data, the wire, the replicated layout and the save format
version are unchanged.

## v60

`world/serialization/EntityPatch.h` is new: `WriteEntityRecord`,
`AuthoredComponentsOf`, `CaptureEntityPatch` and `ApplyEntityPatch`, with
`ComponentPersistenceSet`, `AuthoredComponent`, `EntityScope` and
`EntityPatchOutcome`, write and diff an entity's components as one record.
`ComponentSerializerRegistry` gains `EntitySchema()` and the member that holds
it. `IComponentSerializer` gains the virtual `SaveDefault(IWriteArchive&)`,
declared before the destructor so every later slot moves; `ComponentSerializer<T>`
writes the default-constructed component through it. `SceneSerializationContext`
gains `Purpose` (`SceneRecordPurpose`). `PackageEntity` gains `Unpatched` and
the `Find`, `SetSerialized` and `Remove` members, and `EntityBuildPackage` gains
`FindEntity`. A body's saved patch and a held record's components are written
in component key order, a `Saved` component the body gained is written as its
difference from its default, and a bytes-authored component the body lost is
written as a removal. The save format version, cooked data, the wire and the
replicated layout are unchanged.

## v61

`SavedGameSectionDefinition::Capture` takes `const World&` (was `World&`): a
write settles held records and brings the zone store up to date before any
section captures, and capture reads only, so a section can no longer mint ids
or change the world. `CaptureHeldRecords` takes `const World&` too and no longer
gives a record an id or names its holder; a record without an id after the
settle is logged and left out. A body nothing references is saved without an
id. `participant/ParticipantLifecycle.h` gains `LivingBodyOf`. A write on a
settled level whose local participant has no body fails with "the local
participant has no body". The save format version, cooked data, the wire and
the replicated layout are unchanged.

## v62

Zone state is keyed by level. `world/LevelReference.h` replaces
`app/LevelReference.h`. `zone/ZoneId.h` names the two reserved zone ids,
`kSceneLevelZone` and `kWorldSceneZone`, which the manifest reader and the
world cook refuse. `zone/ZoneStateScope.h` is new: `ZoneStateLevelId` and
`ZoneStateScope`, which `ZoneStateStore::Scope` hands out and every store call,
`CaptureZoneState`, `ParkHeldRecords` and the importer's `stateScope` take in
place of a `ZoneId`. `RuntimeZoneRecord` gains `StateScope`; `ZoneLoadRecipe`
gains `StateScope`; `AsyncZoneLoader::BeginLoad` and `BeginLoadScene` and the
`ImportZonePackage` family take a trailing `ZoneStateScope`.

Level entities are saved by the entity patch. `ZoneComponentMemory`,
`ZoneComponentMemories`, `RestoreZoneState`, `RememberedComponentState` and the
store's component-state methods are gone. `AuthoredEntityState` gains
`Baseline`; `ZoneStateStore::RecordAuthoredSet` takes the authored set by value
and the store gains `RecordPatch`, `ForgetPatchMember`, `PatchOf` and
`BaselineOf`. `ZoneStateSerialization` is a new World resource, installed by the
new `SavedGames::KeepLevelState()`. `EntityPatchOutcome` gains
`MisfitMembers`, and `AuthoredComponent` gains `Diffed`: a capture leaves a
baseline entry marked false out of the patch, which is how a level entity's
component cooked as bytes is not diffed. The responses serializer writes
a binding's `enabled` and `consumed` state in its save form and reads
`consumed` back.

Save format 2. `zone/ZoneStateLists.h` is new: `EncodedZoneLists`,
`ZoneStateList`, the known list names, `kZoneListVersion` and their codecs.
`DurableZoneState` holds a zone's encoded lists and its encoded held records
(`DurableComponentState` is gone), `DurableLevelState` is new, and the store's
`ExportDurable` and `Install` take every level; the store gains `ForgetLevel`,
`CarriedLists` and `LevelOf`. `SavedGame` holds `Levels`, an optional
`Thumbnail` and `Carried` chunks; `SavedGameSummary` gains `Game`;
`DecodeSavedGameSummary` reads a file's prefix. `kSavedGameFormatVersion` is 2
and the oldest version read is 2. `HashedChunkFileWriter` gains `HashChunk`.
`HeldRecordIndex::Await` takes the holder's zone state scope; the index gains
`SetAside`, `Resume`, `AwaitingScopeOf` and `SetAsideRecords`.
`CaptureLevelHeldRecords`, `RestoreLevelHeldRecords` and `ScopedHeldRecords`
are new. `SavedGames::RequestWrite` takes an optional arrival, which
`RestoredBody::Arrival` carries back, and `SavedGames` gains `WritingArrival`;
`LoadedLevel` gains `WorldSceneStateScope`.

`LevelChangeRequest` is a class built only through `Travel(level, memory,
bodies)`, `NewGame(level)` and `Restore(level, remembered)`, with `At(arrival)`
and `Focused(zone)`; `LevelMemory`, `LevelChangeBodies` (one value, `Fresh`),
`LevelChangeKind` and the `LevelArrival` World resource are new.
`LevelChangeRuntimeState` and `LevelChange::RuntimeState()` are gone;
`LevelChange::Kind()` replaces it. `DestroyAwaitingHeldRecords` is replaced by
`SetAsideHeldRecords`, `ResumeHeldRecords` and `DestroySetAsideHeldRecords`.
`ZoneStateStore` gains `Level`.

`IComponentSerializer` gains the virtual `SavedFormEquals(const World&, EntityId,
const JsonValue&)`, declared before the destructor, so every later slot moves;
`CaptureEntityPatch` asks it before writing a baseline component, and the
responses serializer answers it. One bump covers the whole series: ABI 62.


## v63

A verb answers a reason. `VerbAnswer` (`authored/VerbInvocation.h`) is a
`VerbAdmission` and a 16-bit `Detail`, and the dispatcher's erased call returns
it: `VerbDispatcher::Bind` takes an implementation returning either
`VerbAdmission` or `VerbAnswer`. `VerbAnswerOf`, `VerbArgumentAnswer` and
`AnswerOf(VerbInvocationResult)` are new. `VerbInvocation` gains `Result`, an
`AuthoredValue*`; `VerbInvocationResult` and `VerbTraceRecord` gain `Detail`.
`VerbDispatcher::Invoke` takes a trailing `AuthoredValue* result`, and the
dispatcher gains `SetLog`. `VerbDefinition` gains `Refusals` (a vector of the
new `VerbRefusalReason`) and `Result` (an optional `DataFieldSchema`), both
compared by `VerbCatalogTraits::ContractsMatch`; `VerbRefusalsOf`,
`VerbRefusalName` and `DescribeVerbAnswer` are new. `SequenceOutcome` gains
`FirstFailureDetail` and `FirstFailedCommand`. `AbilityTraceRecord::Admission`
and `AnimDecisionRecord::Admission` become `Answer`, a `VerbAnswer`.
`InventoryOperations` and `EquipmentOperations` take no `Logger` and answer
`VerbAnswer`, as do `InvokeSaveWrite` and `InvokeSaveRestore`;
`EngineVocabularyBindings::BindInventory` and `BindEquipment` take no logger.
`SaveAdmission` and `LevelChangeAdmission` gain an `EnumSchema`, and
`core/metadata/EnumSchema.h` gains `FindEnumValue`. `ProjectileLaunchAdmission`
(`projectile/ProjectileVerbs.h`) is new. `ResponsesSystem` gains
`SetLogRefusals`, `LogsRefusals` and `kLoggedRefusals`.

## v64

A sequence names a verb's result and a later command reads it.
`SequenceValueSource` gains `Result`; `SequenceCommandDesc` gains `As`;
`CompiledSequenceValue` and `CompiledSequenceCommand` gain `Result`, a slot
index, with `kNoSequenceResult` for none; `CompiledVerbSequence` gains
`Results`, its slot count. `VerbSequenceRunner` keeps a result slot per name
for the run under way. `inventory.give` and `inventory.take` declare an `entry`
result, which moves their contract revision.

## v65

`ParkHeldRecords` (`world/held/HeldRecords.h`) takes the holders to look at, a
span of entities, instead of a storage partition, so `LoadedLevel::Unload`
parks what a world scene's entities hold before it destroys them.

`LevelChangeBodies` gains `Kept` and `LevelChangeAdmission` gains `BodyInZone`.
`LevelArrival` moves from `app/LevelChange.h` to the new
`participant_spawn/LevelArrival.h` and gains `Kept`, a list of the new
`KeptArrival`. `ActorScaleAt` answers zero while a lease of scale zero is in
force, whatever the limits.

`LevelLandmark` (`world/LevelLandmark.h`, `sencha.level_landmark`) is a new
component in `WorldComponents`, with `LevelLandmarkPlace` and
`FindLevelLandmark`. `LevelArrival` gains `Spawning`. A save written with an
arrival holds no `root` for its body, and its summary's focus is the zone the
landmark stands in; the save format does not change.

The `level.change` verb (`app/LevelChangeVocabulary.h`: `kLevelChangeVerb`,
`DeclareLevelChangeVerbs`, `InvokeLevelChange`) joins the engine's catalog,
answering `LevelChangeAdmission` as its reason, and
`EngineVocabularyBindings` gains `BindLevelChanges`.

## v66

`KeptWithZone` (`zone/KeptWithZone.h`, `sencha.kept_with_zone`) is a new tag
component in `WorldComponents`, with `KeepWithZone`, `IsKeptWithZone` and
`ForgettingOfKept`. `ZoneStateStore` gains `GainedEntityState`,
`RecordGained`, `ForgetGained` and `GainedOf`, and loses `CarriedLists`; its
private zone record gains its gained members. `zone/ZoneStateLists.h` gains
`kWholeGainedForm`, `EncodeWholeGainedMember`, `DecodeWholeGainedMember` and
`WholeGainedMemberId`. `world/serialization/EntityPatch.h` gains
`ReadEntityRecord`. The save format does not change: the `gained` list format 2
reserved now holds `whole` members.

`ZonePlacement` (`zone/ZonePlacement.h`) is a new World resource, which
`LoadedLevel` installs and answers from the new `LoadedLevel::ZoneAt`;
`LoadedLevel`'s destructor is no longer defaulted. `InnermostZoneAt` takes an
optional filter on the zones it considers. `inventory.drop` gains `keep`,
which moves its contract revision.

## v67

`SavedGames` gains `SetSummary`, `RequestList`, `RequestThumbnail`,
`RequestDelete` and `RequestCopy`, and its private in-flight write record
gains the copy's source and a work function; it keeps the slot of the restore
under way and the slots deleted while a listing is outstanding; `Holds` reads
file prefixes.
`SavedGameSummaryDefinition` is new. `save/SaveSlotListing.h` is new:
`SaveSlotSummary`, `SaveSlotListing`, `ListSaveSlots`, `ReadSaveSlotSummary`
and `ReadSaveSlotThumbnail`. `save/SavedGame.h` gains `SavedGameThumbnail`,
`EncodeSavedGameThumbnail`, `DecodeSavedGameThumbnail` and
`SavedGamePrefixWanted`, and `DecodedSavedGameSummary` carries the thumbnail
chunk in place of `HasThumbnail`. `SaveSlotFiles` gains `ReadPrefix`,
`ReadBackupPrefix`, `Remove`, `RemoveBackup` and `Slots`, and
`core/io/FileBytes.h` gains `ReadFilePrefix`. The `save.delete` and
`save.copy` verbs (`kSaveDeleteVerb`, `kSaveCopyVerb`, `InvokeSaveDelete`,
`InvokeSaveCopy`) join the engine's catalog, answering `SaveAdmission` as
their reason, and `BindSavedGames` binds them. The save format does not change:
the `game` member and the `THMB` chunk format 2 reserved are now written.
