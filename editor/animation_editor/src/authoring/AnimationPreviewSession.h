#pragma once

#include "authoring/AnimationScenario.h"

#include <anim/AnimContentSystem.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimEventSystem.h>
#include <anim/AnimPoseSystem.h>
#include <anim/AnimSelectSystem.h>
#include <anim/AnimRequests.h>
#include <anim/AnimRigBinding.h>
#include <assets/data/DataAssetCache.h>
#include <authored/VerbDispatcher.h>
#include <ecs/StoragePartitionSet.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

class CharacterMoverPool;
class GameplayTagRegistry;
class PhysicsWorld;
class World;

struct AnimationPreviewActionOutcome
{
    AnimationScenarioActionKind Kind = AnimationScenarioActionKind::SetFact;
    std::string Subject;
    AnimRequestResult Request;
    bool Cancelled = false;
    // Not issued: this session receives its requests from an authority.
    bool Remote = false;
    // Empty when the action applied; otherwise why not.
    std::string Problem;

    friend bool operator==(const AnimationPreviewActionOutcome&, const AnimationPreviewActionOutcome&) = default;
};

// One invocation a preview recorder accepted, as text.
struct AnimationPreviewInvocation
{
    AnimTick Tick = 0;
    std::string Verb;
    std::string Binding;
    std::string Producer;
    std::string Instigator;
    // In the verb's declared argument order.
    std::vector<std::pair<std::string, std::string>> Arguments;

    friend bool operator==(const AnimationPreviewInvocation&, const AnimationPreviewInvocation&) = default;
};

struct AnimationPreviewLayerRecord
{
    std::uint16_t Winner = kAnimNoRule;
    GameplayTagId Behavior;
    AnimLatchState Latch = AnimLatchState::None;
    std::uint16_t Row = kAnimNoContent;
    std::uint16_t Content = kAnimNoContent;
    // The content's clip, or its flow's current section's.
    std::uint16_t Clip = kAnimNoContent;
    float TimeSeconds = 0.0f;
    bool ContentComplete = false;
    AnimLayerFlow Flow;
    float Weight = 1.0f;
    // kAnimNoRule when the weight is the rig's constant.
    std::uint16_t WeightRule = kAnimNoRule;
    // One per flattened rule; empty on a request-keyed layer.
    std::vector<AnimRuleVerdict> Verdicts;
};

struct AnimationPreviewMovementRecord
{
    // Capsule centre after the tick; yaw is about +Y.
    Vec3d Position = Vec3d::Zero();
    float Yaw = 0.0f;
    // Ground displacement root motion asked for and the mover achieved.
    Vec3d Requested = Vec3d::Zero();
    Vec3d Achieved = Vec3d::Zero();
    bool Carried = false;
    bool Blocked = false;

    friend bool operator==(const AnimationPreviewMovementRecord&, const AnimationPreviewMovementRecord&) = default;
};

// Compared whole when a replay is checked against the take it came from.
struct AnimationPreviewTickRecord
{
    AnimTick Tick = 0;
    std::vector<std::uint32_t> Facts;
    bool FactsExact = false;
    std::vector<AnimRequest> Requests;
    std::vector<AnimDecisionRecord> Decisions;
    std::vector<AnimationPreviewActionOutcome> Actions;
    std::vector<AnimationPreviewLayerRecord> Layers;
    // In dispatch order.
    std::vector<AnimationPreviewInvocation> Invocations;
    // Composed local pose; empty when the rig names no loaded skeleton.
    std::vector<Transform3f> Pose;
    std::optional<AnimationPreviewMovementRecord> Movement;
};

[[nodiscard]] bool SameAnimationPreviewTick(const AnimationPreviewTickRecord& a,
                                            const AnimationPreviewTickRecord& b);

class AnimationPreviewSession
{
public:
    // `vocabulary` registers the project's names into each preview World.
    // Without `clips` rigs bind but resolve no content.
    explicit AnimationPreviewSession(const DataAssetCache& data,
                                     const AnimationClipCache* clips = nullptr,
                                     std::function<void(World&)> vocabulary = {},
                                     const SkeletonCache* skeletons = nullptr);
    ~AnimationPreviewSession();

    AnimationPreviewSession(const AnimationPreviewSession&) = delete;
    AnimationPreviewSession& operator=(const AnimationPreviewSession&) = delete;

    // False, with the session empty and Problems() set, when the rig is not loaded.
    bool Open(AnimationScenario scenario);
    void Close();

    void Restart();
    void Step();
    // Restarts first when `tick` is behind the current tick.
    void RunTo(AnimTick tick);
    // Speed scales how quickly fixed ticks are scheduled, never their length.
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
    // Takes effect on the next restart.
    bool AddParticipant(const std::string& name);
    // Tag declarations, role, request source, movement and recorders are
    // scenario state: changing one replays the scenario to the current tick.
    bool DeclareTag(const std::string& name, std::string* error = nullptr);
    bool UndeclareTag(const std::string& name);
    void SetRole(AnimationPreviewRole role);
    // True for a client session whose requests arrive from another session's authority.
    void SetRequestsFromWire(bool fromWire);
    void SetMovement(std::optional<AnimationScenarioMovement> movement);
    [[nodiscard]] bool RequestsFromWire() const { return FromWire; }
    void SetRecorder(std::string_view verb, bool attached);
    [[nodiscard]] bool HasRecorder(std::string_view verb) const;

    // Both evaluate disposable copies of this tick's state plus the scheduled
    // edits; neither commits anything.
    [[nodiscard]] std::vector<std::uint32_t> PreviewNextTick();
    [[nodiscard]] std::vector<std::vector<AnimRuleVerdict>> ExplainNextTick();
    // Call after a rig asset changed in the cache, so panels see the new
    // binding before the next tick.
    void Rebind();
    void VocabularyChanged();

    [[nodiscard]] bool IsOpen() const { return Preview != nullptr; }
    [[nodiscard]] bool IsPlaying() const { return Playing; }
    [[nodiscard]] double Speed() const { return PlaybackSpeed; }
    [[nodiscard]] AnimTick Tick() const { return CurrentTick; }
    [[nodiscard]] AnimTick NextTick() const { return CurrentTick + 1; }
    [[nodiscard]] double TickSeconds() const;
    [[nodiscard]] const AnimationScenario& Scenario() const { return Working; }
    [[nodiscard]] bool ScenarioModified() const;
    void MarkScenarioSaved();

    // Null when the rig did not bind.
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
    [[nodiscard]] const std::vector<AnimDiagnostic>& ScenarioProblems() const { return ScenarioIssues; }
    [[nodiscard]] const AnimationScenarioValue* Input(std::string_view fact) const;
    [[nodiscard]] EntityId Subject() const { return SubjectEntity; }
    // Null unless the scenario moves the subject.
    [[nodiscard]] const Transform3f* SubjectTransform() const;
    // Capsule height; the feet are half of it below the transform.
    [[nodiscard]] float SubjectHeight() const;
    [[nodiscard]] const AnimPosePool::Slot* SubjectPose() const;
    [[nodiscard]] const AnimPoseState* SubjectPoseState() const;
    [[nodiscard]] EntityId ParticipantEntity(std::string_view name) const;
    [[nodiscard]] std::string_view ParticipantName(EntityId entity) const;
    [[nodiscard]] const GameplayTagRegistry* Tags() const;
    // Rebuilt on every restart; null while closed.
    [[nodiscard]] World* SimulationWorld() { return Preview.get(); }
    [[nodiscard]] const World* SimulationWorld() const { return Preview.get(); }
    [[nodiscard]] const VerbRegistry* Verbs() const;

    static constexpr std::size_t kHistoryCapacity = 3600;

private:
    // Accepts every invocation of one verb and records it into the running tick.
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
    AnimationPreviewMovementRecord StepMovement(AnimTick tick);
    // Restarts and runs back to the current tick.
    void Replay();
    void RunTick(AnimTick tick);
    AnimationPreviewActionOutcome Apply(const AnimationScenarioAction& action, std::size_t index,
                                        AnimTick tick);
    struct Refusal
    {
        std::string Code;
        std::string Message;
    };
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
    std::string SavedScenarioText;
    // Lets several edits made while paused on one tick share one branch.
    std::optional<AnimTick> BranchedAt;

    std::unique_ptr<World> Preview;
    // Declared after the World and before the tokens: teardown unbinds, then
    // drops the dispatcher, then the World.
    std::unique_ptr<VerbDispatcher> Dispatcher;
    std::deque<Recorder> Recorders;
    std::vector<VerbBindingToken> RecorderTokens;
    // Set only while a tick drains its events.
    std::vector<AnimationPreviewInvocation>* InvocationSink = nullptr;
    // Built with the World and dropped with it, so no system's cached queries
    // outlive the World they were made on.
    struct WorldSystems;
    std::unique_ptr<WorldSystems> Systems;
    const AnimBoundRig* Bound = nullptr;
    EntityId SubjectEntity;
    std::vector<std::pair<std::string, EntityId>> Participants;
    std::deque<InputSlot> Slots;

    InputTable Inputs;
    // Points at a disposable copy while PreviewNextTick evaluates.
    const InputTable* ActiveInputs = &Inputs;

    AnimTick CurrentTick = 0;
    std::uint64_t LogWritten = 0;
    std::deque<AnimationPreviewTickRecord> Records;
    std::vector<AnimDiagnostic> ScenarioIssues;

    bool Playing = false;
    double PlaybackSpeed = 1.0;
    double PendingTicks = 0.0;
    bool FromWire = false;

    // Torn down before the World, whose movers they hold.
    std::unique_ptr<PhysicsWorld> Physics;
    std::unique_ptr<CharacterMoverPool> Movers;
    StoragePartitionSet AllPartitions;
};
