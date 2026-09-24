#pragma once

#include "authoring/AnimationScenario.h"

#include <anim/AnimContentSystem.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimEventSystem.h>
#include <anim/AnimPoseSystem.h>
#include <anim/AnimFactGatherSystem.h>
#include <anim/AnimSelectSystem.h>
#include <anim/AnimRequests.h>
#include <anim/AnimRigBinding.h>
#include <assets/data/DataAssetCache.h>
#include <authored/VerbDispatcher.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

class GameplayTagRegistry;
class World;

//=============================================================================
// AnimationPreviewSession
//
// A rig under a scenario, simulated in an isolated World on a fixed clock with
// the production binding, gather, derivation, request, selection, content
// resolution and event code. The editor substitutes only gameplay: the
// preview's fact providers read scenario-owned inputs, scenario participants
// issue requests through the normal request API, and the preview's dispatcher
// has no implementation behind a verb except the recorders the scenario names.
// A verb without one answers Unavailable, as it would in a World nothing
// implements it in.
//
// The live session is a scenario run. Every live edit is appended to the
// working scenario at the next tick and applied when that tick runs, so what
// the author did and what a replay of the saved scenario does are the same
// code on the same ticks. Acting at a tick the scenario has already scripted
// past drops the later actions: a new branch from there.
//
// Nothing here writes an asset or a document. Saving the scenario is explicit
// and goes to its own sidecar.
//=============================================================================

// One action as it applied, for the timeline and the request console.
struct AnimationPreviewActionOutcome
{
    AnimationScenarioActionKind Kind = AnimationScenarioActionKind::SetFact;
    std::string Subject;
    // Requests: what the request API decided.
    AnimRequestResult Request;
    bool Cancelled = false;
    // Empty when the action applied; otherwise why not.
    std::string Problem;

    friend bool operator==(const AnimationPreviewActionOutcome&, const AnimationPreviewActionOutcome&) = default;
};

// What a preview recorder was handed: one invocation, as text. Never proof
// that a game did anything -- only that the authored path reached the verb
// with these values.
struct AnimationPreviewInvocation
{
    AnimTick Tick = 0;
    std::string Verb;
    std::string Binding;
    // The animated entity and the participant behind the request, by name.
    std::string Producer;
    std::string Instigator;
    // Each argument the verb declares, in its order, and its value.
    std::vector<std::pair<std::string, std::string>> Arguments;

    friend bool operator==(const AnimationPreviewInvocation&, const AnimationPreviewInvocation&) = default;
};

// One layer's outcome on one tick: what won and why every rule did or did not,
// and what that resolved to. The recorded half of the decision debugger.
struct AnimationPreviewLayerRecord
{
    std::uint16_t Winner = kAnimNoRule;
    GameplayTagId Behavior;
    AnimLatchState Latch = AnimLatchState::None;
    std::uint16_t Row = kAnimNoContent;
    std::uint16_t Content = kAnimNoContent;
    // The clip playing: the content's, or its flow's current section's.
    std::uint16_t Clip = kAnimNoContent;
    float TimeSeconds = 0.0f;
    bool ContentComplete = false;
    AnimLayerFlow Flow;
    // The layer's weight, and the weight rule that set it or none for the
    // rig's constant.
    float Weight = 1.0f;
    std::uint16_t WeightRule = kAnimNoRule;
    // One per flattened rule; empty on a request-keyed layer.
    std::vector<AnimRuleVerdict> Verdicts;
};

// Everything observable about one tick, kept for history and compared whole
// when a replay is checked against the take it came from.
struct AnimationPreviewTickRecord
{
    AnimTick Tick = 0;
    std::vector<std::uint32_t> Facts;
    bool FactsExact = false;
    std::vector<AnimRequest> Requests;
    std::vector<AnimDecisionRecord> Decisions;
    std::vector<AnimationPreviewActionOutcome> Actions;
    std::vector<AnimationPreviewLayerRecord> Layers;
    // What preview recorders were handed this tick, in dispatch order.
    std::vector<AnimationPreviewInvocation> Invocations;
    // The subject's composed local pose, empty when its rig names no loaded
    // skeleton.
    std::vector<Transform3f> Pose;
};

[[nodiscard]] bool SameAnimationPreviewTick(const AnimationPreviewTickRecord& a,
                                            const AnimationPreviewTickRecord& b);

class AnimationPreviewSession
{
public:
    // `vocabulary` installs the project's gameplay vocabulary into each
    // preview World -- the names content and scenarios may resolve against.
    // Nothing a scenario names is ever registered on its behalf.
    // `clips` gives slot rows their content; without it rigs bind but resolve
    // nothing. `skeletons` gives masks their joints.
    explicit AnimationPreviewSession(const DataAssetCache& data,
                                     const AnimationClipCache* clips = nullptr,
                                     std::function<void(World&)> vocabulary = {},
                                     const SkeletonCache* skeletons = nullptr);
    ~AnimationPreviewSession();

    AnimationPreviewSession(const AnimationPreviewSession&) = delete;
    AnimationPreviewSession& operator=(const AnimationPreviewSession&) = delete;

    // Replaces the scenario and restarts. False when its rig is not a loaded
    // rig; the session is then empty and Problems() says why.
    bool Open(AnimationScenario scenario);
    void Close();

    // Rebuilds the World and runs the scenario's tick 0.
    void Restart();
    // Runs the next tick.
    void Step();
    // Runs to `tick` from a restart when it is behind the current one.
    void RunTo(AnimTick tick);
    // Schedules fixed ticks against wall time. Speed scales how quickly ticks
    // are scheduled, never their length.
    void Advance(double wallSeconds);
    void Play() { Playing = true; }
    void Pause();
    [[nodiscard]] bool SetSpeed(double speed);

    // Live edits, applied at NextTick().
    void SetFact(const std::string& fact, AnimationScenarioValue value);
    void ClearFact(const std::string& fact);
    void IssueRequest(AnimationScenarioAction request);
    void CancelRequest(const std::string& participant, const std::string& intent,
                       AnimCancelReason reason);
    // Adds a participant to the scenario, for the next restart.
    bool AddParticipant(const std::string& name);
    // Declares a gameplay tag as a preview fixture -- a name a game module
    // would declare, registered only in the preview World -- or stops
    // declaring one. Tags register when the World is built, so either replays
    // the scenario to the current tick. False, with `error` saying why, for a
    // malformed name, one already declared, or one that was not.
    bool DeclareTag(const std::string& name, std::string* error = nullptr);
    bool UndeclareTag(const std::string& name);
    // The preview World's role, and whether a recorder stands behind a verb.
    // Both change what a run does, so both are scenario state: changing one
    // replays the scenario to the current tick under the new setting.
    void SetRole(AnimationPreviewRole role);
    void SetRecorder(std::string_view verb, bool attached);
    [[nodiscard]] bool HasRecorder(std::string_view verb) const;

    // The facts the next tick would produce, from a disposable copy of this
    // tick's state and the edits scheduled for it. Commits nothing.
    [[nodiscard]] std::vector<std::uint32_t> PreviewNextTick();
    // Per layer, how every rule would fare next tick, from the same
    // disposable copies. What a changed fact or an edited rule does, shown
    // before time moves.
    [[nodiscard]] std::vector<std::vector<AnimRuleVerdict>> ExplainNextTick();
    // Re-resolves the rig after one of its assets changed in the cache, so
    // panels read the new binding before the next tick runs.
    void Rebind();

    [[nodiscard]] bool IsOpen() const { return Preview != nullptr; }
    [[nodiscard]] bool IsPlaying() const { return Playing; }
    [[nodiscard]] double Speed() const { return PlaybackSpeed; }
    [[nodiscard]] AnimTick Tick() const { return CurrentTick; }
    [[nodiscard]] AnimTick NextTick() const { return CurrentTick + 1; }
    [[nodiscard]] double TickSeconds() const;
    [[nodiscard]] const AnimationScenario& Scenario() const { return Working; }
    [[nodiscard]] bool ScenarioModified() const;
    void MarkScenarioSaved();

    // The rig as the preview World bound it. Null when it did not bind.
    [[nodiscard]] const AnimBoundRig* Rig() const { return Bound; }
    [[nodiscard]] std::span<const std::uint32_t> Facts() const;
    [[nodiscard]] bool FactsExact() const;
    [[nodiscard]] const AnimRequestSet* Requests() const;
    [[nodiscard]] const AnimDecisionLog* DecisionLog() const;
    [[nodiscard]] const AnimSelectorState* Selection() const;
    [[nodiscard]] const AnimContentState* Content() const;
    [[nodiscard]] const std::deque<AnimationPreviewTickRecord>& History() const { return Records; }
    // The rig's diagnostics, then the scenario's, deduplicated.
    [[nodiscard]] std::vector<AnimDiagnostic> Problems() const;
    // Scenario-level problems only (unknown fact, intent, participant...).
    [[nodiscard]] const std::vector<AnimDiagnostic>& ScenarioProblems() const { return ScenarioIssues; }
    // The current input for a gathered fact, if the scenario sets one.
    [[nodiscard]] const AnimationScenarioValue* Input(std::string_view fact) const;
    [[nodiscard]] EntityId Subject() const { return SubjectEntity; }
    // The subject's pose storage and pose state, once a pass has posed it.
    [[nodiscard]] const AnimPosePool::Slot* SubjectPose() const;
    [[nodiscard]] const AnimPoseState* SubjectPoseState() const;
    [[nodiscard]] EntityId ParticipantEntity(std::string_view name) const;
    // The participant a request source is, or empty.
    [[nodiscard]] std::string_view ParticipantName(EntityId entity) const;
    // The preview World's vocabulary, for showing tag values by name.
    [[nodiscard]] const GameplayTagRegistry* Tags() const;
    // The verbs the preview World declares: the engine's and the project's.
    [[nodiscard]] const VerbRegistry* Verbs() const;

    // How many ticks of history are kept.
    static constexpr std::size_t kHistoryCapacity = 3600;

private:
    // Stands behind one declared verb: accepts, and records the invocation
    // as text into the tick being run.
    struct Recorder
    {
        AnimationPreviewSession* Session = nullptr;
        std::string Verb;
        VerbAdmission Invoke(const VerbInvocation& invocation);
    };

    struct InputSlot
    {
        const AnimationPreviewSession* Session = nullptr;
        std::string Fact;
        AnimFactKind Kind = AnimFactKind::Float;
    };
    using InputTable = std::unordered_map<std::string, AnimationScenarioValue>;

    static bool ReadInput(const World& world, EntityId entity, const void* context, std::uint32_t& out);

    void BuildWorld();
    void DropWorld();
    // Restarts and runs back to the tick the session was on.
    void Replay();
    void RunTick(AnimTick tick);
    AnimationPreviewActionOutcome Apply(const AnimationScenarioAction& action, std::size_t index,
                                        AnimTick tick);
    struct Refusal
    {
        std::string Code;
        std::string Message;
    };
    // Validates a fact action against the bound rig and applies it to `inputs`.
    [[nodiscard]] std::optional<Refusal> ApplyFact(AnimationScenarioActionKind kind,
                                                   const std::string& fact,
                                                   const AnimationScenarioValue& value,
                                                   InputTable& inputs) const;
    void Schedule(AnimationScenarioAction action);
    void Problem(std::string code, std::string field, std::string message);
    [[nodiscard]] std::string ScenarioField(std::size_t actionIndex, std::string_view key) const;

    const DataAssetCache& Data;
    const AnimationClipCache* Clips = nullptr;
    const SkeletonCache* Skeletons = nullptr;
    std::function<void(World&)> Vocabulary;

    AnimationScenario Working;
    std::string SavedForm;
    // The tick live edits last branched the scenario at, so several edits
    // made while paused on one tick all survive.
    std::optional<AnimTick> BranchedAt;

    std::unique_ptr<World> Preview;
    // Over the preview World's catalog, with recorders the only
    // implementations: nothing here can run the game. Declared after the
    // World and before the tokens, so teardown unbinds, then drops the
    // dispatcher, then the World.
    std::unique_ptr<VerbDispatcher> Dispatcher;
    std::deque<Recorder> Recorders;
    std::vector<VerbBindingToken> RecorderTokens;
    std::vector<AnimPendingEvent> PendingEvents;
    // Where recorders write during a tick's drain.
    std::vector<AnimationPreviewInvocation>* InvocationSink = nullptr;
    AnimFactGatherSystem Gather;
    // Inline: the preview is the serial reference.
    AnimPoseSystem Poser;
    const AnimBoundRig* Bound = nullptr;
    EntityId SubjectEntity;
    std::vector<std::pair<std::string, EntityId>> Participants;
    std::deque<InputSlot> Slots;

    InputTable Inputs;
    // What the providers read: the live table, or a disposable copy while
    // PreviewNextTick evaluates.
    const InputTable* ActiveInputs = &Inputs;

    AnimTick CurrentTick = 0;
    std::uint64_t LogWritten = 0;
    std::deque<AnimationPreviewTickRecord> Records;
    std::vector<AnimDiagnostic> ScenarioIssues;

    bool Playing = false;
    double PlaybackSpeed = 1.0;
    double PendingTicks = 0.0;
};
