#include "authoring/AnimationPreviewSession.h"

#include <abilities/AbilityKit.h>
#include <anim/AnimFactEvaluation.h>
#include <anim/AnimFactProviders.h>
#include <anim/AnimationRegistration.h>
#include <app/EngineVerbs.h>
#include <authored/WorldVocabulary.h>
#include <core/json/JsonStringify.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/ComponentRegistrar.h>
#include <world/SimulationAuthority.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace
{
    constexpr int kMaxTicksPerAdvance = 8;
    // One entity crossing more marks than this on one tick is refused on the
    // record, as the runtime refuses past its queue capacity.
    constexpr std::size_t kPreviewEventCapacity = 256;

    // A verb argument as a reader would write it.
    std::string ValueText(const VerbValue& value, const GameplayTagRegistry* tags)
    {
        const auto list = [&](std::string_view open, std::string_view close) {
            std::string text(open);
            for (std::size_t i = 0; i < value.Children().size(); ++i)
                text += (i == 0 ? "" : ", ") + ValueText(value.Children()[i], tags);
            return text + std::string(close);
        };
        bool flag = false;
        std::int64_t whole = 0;
        double number = 0.0;
        std::string_view text;
        VerbVectorValue vector;
        const AssetRef* asset = nullptr;
        GameplayTagId tag;
        EntityId entity;
        switch (value.Kind())
        {
        case VerbValueKind::None: return "(none)";
        case VerbValueKind::Bool: return value.TryGetBool(flag) && flag ? "true" : "false";
        case VerbValueKind::Int: return value.TryGetInt(whole) ? std::format("{}", whole) : "?";
        case VerbValueKind::Float: return value.TryGetFloat(number) ? std::format("{}", number) : "?";
        case VerbValueKind::String: return value.TryGetString(text) ? std::format("\"{}\"", text) : "?";
        case VerbValueKind::Enum: return value.TryGetEnum(text) ? std::string(text) : "?";
        case VerbValueKind::Vector:
        {
            if (!value.TryGetVector(vector))
                return "?";
            std::string out = "(";
            for (std::size_t i = 0; i < vector.Length; ++i)
                out += std::format("{}{}", i == 0 ? "" : ", ", vector.Components[i]);
            return out + ")";
        }
        case VerbValueKind::Record: return list("{", "}");
        case VerbValueKind::Array: return list("[", "]");
        case VerbValueKind::AssetRef:
            return value.TryGetAsset(asset) ? asset->Path : "?";
        case VerbValueKind::DataAssetRef:
            return value.TryGetDataAsset(asset) ? asset->Path : "?";
        case VerbValueKind::GameplayTag:
            if (value.TryGetTag(tag) && tags != nullptr)
                return std::string(tags->GetName(tag));
            return "?";
        case VerbValueKind::Entity:
            return value.TryGetEntity(entity) ? std::format("entity {}", entity.Index) : "?";
        case VerbValueKind::PersistentEntity: return "(persistent entity)";
        }
        return "?";
    }

    bool SameRequest(const AnimRequest& a, const AnimRequest& b)
    {
        return a.Id == b.Id && a.Intent == b.Intent && a.SourceTag == b.SourceTag
            && a.StartTick == b.StartTick && a.FixedTicks == b.FixedTicks
            && std::equal(std::begin(a.Params), std::end(a.Params), std::begin(b.Params))
            && a.CancelTick == b.CancelTick && a.TailUntilTick == b.TailUntilTick
            && a.AnchorSectionStartTick == b.AnchorSectionStartTick && a.Layers == b.Layers
            && a.Lifetime == b.Lifetime && a.CancelReason == b.CancelReason
            && a.AnchorSection == b.AnchorSection && a.Occupied == b.Occupied;
    }

    bool SameDecision(const AnimDecisionRecord& a, const AnimDecisionRecord& b)
    {
        return a.Tick == b.Tick && a.Cause == b.Cause && a.Layer == b.Layer
            && a.Request == b.Request && a.Intent == b.Intent && a.CancelReason == b.CancelReason
            && a.RejectReason == b.RejectReason && a.Reason == b.Reason && a.Rule == b.Rule
            && a.PreviousRule == b.PreviousRule && a.Behavior == b.Behavior && a.Row == b.Row
            && a.Content == b.Content && a.EventKey == b.EventKey && a.EventOutcome == b.EventOutcome
            && a.Admission == b.Admission && a.Section == b.Section && a.PreviousSection == b.PreviousSection;
    }

    template <typename T, typename F>
    bool SameRange(const std::vector<T>& a, const std::vector<T>& b, F same)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), same);
    }

    std::string_view ValueKindName(const AnimationScenarioValue& value)
    {
        switch (value.Type)
        {
        case AnimationScenarioValue::Kind::Bool: return "a bool";
        case AnimationScenarioValue::Kind::Number: return "a number";
        case AnimationScenarioValue::Kind::Name: return "a name";
        }
        return "a value";
    }

    double AsNumber(const AnimationScenarioValue& value)
    {
        return value.Type == AnimationScenarioValue::Kind::Bool ? (value.Bool ? 1.0 : 0.0)
                                                               : value.Number;
    }

    // Encodes a scenario value as a slot or parameter of `kind` holds it. Null
    // when the value cannot be one: a name for a number, a tag nobody declared.
    std::optional<std::uint32_t> Encode(const AnimationScenarioValue& value, AnimFactKind kind,
                                        const GameplayTagRegistry* tags)
    {
        const bool numeric = value.Type != AnimationScenarioValue::Kind::Name;
        switch (kind)
        {
        case AnimFactKind::Bool:
            return numeric ? std::optional(AnimFactFromBool(AsNumber(value) != 0.0)) : std::nullopt;
        case AnimFactKind::Float:
            return numeric ? std::optional(AnimFactFromFloat(static_cast<float>(AsNumber(value))))
                           : std::nullopt;
        case AnimFactKind::Int:
            return numeric ? std::optional(AnimFactFromInt(
                                 static_cast<std::int32_t>(std::lround(AsNumber(value)))))
                           : std::nullopt;
        case AnimFactKind::Tag:
        {
            if (value.Type != AnimationScenarioValue::Kind::Name || tags == nullptr)
                return std::nullopt;
            const GameplayTagId tag = tags->FindTag(value.Name);
            return tag.IsValid() ? std::optional(tag.Value) : std::nullopt;
        }
        case AnimFactKind::TagSet:
            return std::nullopt;
        }
        return std::nullopt;
    }

    AnimFactKind ParamKind(AnimRequestParamKind kind)
    {
        switch (kind)
        {
        case AnimRequestParamKind::Float: return AnimFactKind::Float;
        case AnimRequestParamKind::Int: return AnimFactKind::Int;
        case AnimRequestParamKind::Bool: return AnimFactKind::Bool;
        case AnimRequestParamKind::Tag: return AnimFactKind::Tag;
        }
        return AnimFactKind::Float;
    }
}

bool SameAnimationPreviewTick(const AnimationPreviewTickRecord& a, const AnimationPreviewTickRecord& b)
{
    const auto sameLayer = [](const AnimationPreviewLayerRecord& x, const AnimationPreviewLayerRecord& y) {
        return x.Winner == y.Winner && x.Behavior == y.Behavior && x.Latch == y.Latch && x.Row == y.Row
            && x.Content == y.Content && x.Clip == y.Clip && x.TimeSeconds == y.TimeSeconds
            && x.ContentComplete == y.ContentComplete && x.Flow.Section == y.Flow.Section
            && x.Flow.SectionStartTick == y.Flow.SectionStartTick && x.Flow.LoopCount == y.Flow.LoopCount
            && x.Flow.Phase == y.Flow.Phase && x.Weight == y.Weight && x.WeightRule == y.WeightRule
            && SameRange(x.Verdicts, y.Verdicts, [](const AnimRuleVerdict& p, const AnimRuleVerdict& q) {
                   return p.Kind == q.Kind && p.Stayed == q.Stayed && p.Evaluation.FailedRow == q.Evaluation.FailedRow;
               });
    };
    return a.Tick == b.Tick && a.Facts == b.Facts && a.FactsExact == b.FactsExact
        && SameRange(a.Requests, b.Requests, SameRequest)
        && SameRange(a.Decisions, b.Decisions, SameDecision) && a.Actions == b.Actions
        && SameRange(a.Layers, b.Layers, sameLayer) && a.Invocations == b.Invocations;
}

AnimationPreviewSession::AnimationPreviewSession(const DataAssetCache& data, const AnimationClipCache* clips,
                                                 std::function<void(World&)> vocabulary,
                                                 const SkeletonCache* skeletons)
    : Data(data)
    , Clips(clips)
    , Skeletons(skeletons)
    , Vocabulary(std::move(vocabulary))
{
}

AnimationPreviewSession::~AnimationPreviewSession() = default;

bool AnimationPreviewSession::Open(AnimationScenario scenario)
{
    Working = std::move(scenario);
    SavedForm = JsonStringify(WriteAnimationScenario(Working));
    Restart();
    return Bound != nullptr;
}

void AnimationPreviewSession::DropWorld()
{
    // Unbound, then undispatched, then gone: the tokens reach back into the
    // dispatcher, and the dispatcher into the World's catalog.
    RecorderTokens.clear();
    Recorders.clear();
    Dispatcher.reset();
    Preview.reset();
}

void AnimationPreviewSession::Close()
{
    DropWorld();
    Bound = nullptr;
    Records.clear();
    ScenarioIssues.clear();
    Inputs.clear();
    Slots.clear();
    Participants.clear();
    Working = AnimationScenario{};
    SavedForm.clear();
    Playing = false;
}

double AnimationPreviewSession::TickSeconds() const
{
    return 1.0 / static_cast<double>(std::max<std::uint32_t>(Working.TickRate, 1));
}

bool AnimationPreviewSession::ScenarioModified() const
{
    return JsonStringify(WriteAnimationScenario(Working)) != SavedForm;
}

void AnimationPreviewSession::MarkScenarioSaved()
{
    SavedForm = JsonStringify(WriteAnimationScenario(Working));
}

void AnimationPreviewSession::Problem(std::string code, std::string field, std::string message)
{
    for (const AnimDiagnostic& existing : ScenarioIssues)
    {
        if (existing.Code == code && existing.FieldPath == field)
            return;
    }
    ScenarioIssues.push_back(AnimDiagnostic{ AnimDiagnosticSeverity::Error, std::move(code),
                                             Working.Name, std::move(field), std::move(message) });
}

std::string AnimationPreviewSession::ScenarioField(std::size_t actionIndex, std::string_view key) const
{
    return std::format("$.actions[{}].{}", actionIndex, key);
}

bool AnimationPreviewSession::ReadInput(const World& world, EntityId, const void* context,
                                        std::uint32_t& out)
{
    const auto* slot = static_cast<const InputSlot*>(context);
    const InputTable& inputs = *slot->Session->ActiveInputs;
    const auto found = inputs.find(slot->Fact);
    if (found == inputs.end())
        return false;
    const std::optional<std::uint32_t> encoded =
        Encode(found->second, slot->Kind, world.TryGetResource<GameplayTagRegistry>());
    if (!encoded)
        return false;
    out = *encoded;
    return true;
}

void AnimationPreviewSession::BuildWorld()
{
    DropWorld();
    Preview = std::make_unique<World>();
    Bound = nullptr;
    Slots.clear();
    Participants.clear();
    SubjectEntity = EntityId{};
    LogWritten = 0;

    // The same vocabulary and components a runtime World has for animation,
    // then the project's names on top.
    InstallAbilityKitVocabulary(*Preview);
    {
        ComponentRegistrar registrar(*Preview);
        RegisterAbilityKitComponents(registrar);
        RegisterAnimationComponents(registrar);
    }
    InstallAnimationVocabulary(*Preview);
    // The verbs content may name: the engine's, then the project's, declared
    // in the order a runtime World declares them.
    VerbRegistry& verbs = InstallVerbRegistry(*Preview);
    (void)DeclareEngineVerbs(verbs);
    if (Vocabulary)
        Vocabulary(*Preview);
    for (const std::string& error : verbs.InstallationErrors())
        Problem("anim.preview.vocabulary", {}, error);
    verbs.ClearInstallationErrors();
    Preview->SetResource(SimulationAuthority{ Working.Role == AnimationPreviewRole::Authority });

    // Recorders are the only implementations the preview has.
    Dispatcher = std::make_unique<VerbDispatcher>(verbs);
    for (std::size_t i = 0; i < Working.Recorders.size(); ++i)
    {
        const VerbId verb = verbs.Find(Working.Recorders[i]);
        if (!verb.IsValid())
        {
            Problem("anim.scenario.recorder_unknown", std::format("$.recorders[{}]", i),
                    std::format("'{}' is not a verb this preview declares.", Working.Recorders[i]));
            continue;
        }
        Recorders.push_back(Recorder{ this, Working.Recorders[i] });
        RecorderTokens.push_back(Dispatcher->Bind(verb, Recorders.back()));
    }
    GameplayTagRegistry& tags = Preview->GetResource<GameplayTagRegistry>();
    for (std::size_t i = 0; i < Working.DeclaredTags.size(); ++i)
    {
        GameplayTagError error;
        if (!tags.RegisterTag(Working.DeclaredTags[i], &error))
            Problem("anim.scenario.declared_tag", std::format("$.declared_tags[{}]", i), error.Message);
    }
    Preview->SetResource(AnimRigBindings{ &Data, Clips, Skeletons });

    // Participants first, in scenario order, so their local entities are the
    // same on every run of the same scenario.
    for (const std::string& name : Working.Participants)
        Participants.emplace_back(name, Preview->CreateEntity());

    const DataAssetHandle rig = Data.Find(Working.RigPath);
    AnimRigBindings& bindings = Preview->GetResource<AnimRigBindings>();
    const AnimBoundRig* bound = bindings.Resolve(rig, *Preview);
    if (bound == nullptr)
    {
        Problem("anim.scenario.rig_unavailable", "$.rig",
                std::format("'{}' is not a loaded animation rig.", Working.RigPath));
        return;
    }

    // Every gathered slot reads the scenario's input for it; derived facts
    // come from the production derivations.
    AnimFactProviders& providers = Preview->GetResource<AnimFactProviders>();
    for (const AnimBoundFactSlot& slot : bound->Slots)
    {
        if (slot.Derivation >= 0 || slot.Kind == AnimFactKind::TagSet)
            continue;
        Slots.push_back(InputSlot{ this, slot.Name, slot.Kind });
        (void)providers.Bind(slot.Name, slot.Kind, &ReadInput, &Slots.back());
    }
    bound = bindings.Resolve(rig, *Preview);

    SubjectEntity = Preview->CreateEntity();
    Preview->AddComponent(SubjectEntity, AnimRig{ rig });
    if (bound->HasFacts)
    {
        if (bound->Capacity == AnimFactCapacity::Large)
            Preview->AddComponent(SubjectEntity, AnimFactsLarge{});
        else
            Preview->AddComponent(SubjectEntity, AnimFacts{});
    }
    Preview->AddComponent(SubjectEntity, AnimDecisionLog{});
    Bound = bound;
}

void AnimationPreviewSession::Restart()
{
    Records.clear();
    ScenarioIssues.clear();
    Inputs.clear();
    CurrentTick = 0;
    BranchedAt.reset();
    PendingTicks = 0.0;
    BuildWorld();
    if (Bound == nullptr)
        return;

    for (const auto& [fact, value] : Working.Inputs)
    {
        if (std::optional<Refusal> refusal =
                ApplyFact(AnimationScenarioActionKind::SetFact, fact, value, Inputs))
            Problem(refusal->Code, std::format("$.inputs.{}", fact), refusal->Message);
    }
    RunTick(0);
}

void AnimationPreviewSession::Step()
{
    if (Preview == nullptr || Bound == nullptr)
        return;
    BranchedAt.reset();
    RunTick(CurrentTick + 1);
}

void AnimationPreviewSession::RunTo(AnimTick tick)
{
    if (Preview == nullptr)
        return;
    if (tick < CurrentTick)
        Restart();
    while (Bound != nullptr && CurrentTick < tick)
        Step();
}

void AnimationPreviewSession::Advance(double wallSeconds)
{
    if (!Playing || Bound == nullptr)
        return;
    PendingTicks += wallSeconds * PlaybackSpeed * static_cast<double>(Working.TickRate);
    // A long frame schedules a bounded number of ticks and drops the rest,
    // rather than spiralling to catch up with a stall.
    PendingTicks = std::min(PendingTicks, static_cast<double>(kMaxTicksPerAdvance));
    while (PendingTicks >= 1.0)
    {
        PendingTicks -= 1.0;
        Step();
    }
}

void AnimationPreviewSession::Pause()
{
    Playing = false;
    PendingTicks = 0.0;
}

bool AnimationPreviewSession::SetSpeed(double speed)
{
    if (!(speed > 0.0) || speed > 8.0)
        return false;
    PlaybackSpeed = speed;
    return true;
}

void AnimationPreviewSession::Schedule(AnimationScenarioAction action)
{
    if (!BranchedAt || *BranchedAt != CurrentTick)
    {
        Working.TruncateAfter(CurrentTick);
        BranchedAt = CurrentTick;
    }
    action.Tick = NextTick();
    Working.Append(std::move(action));
}

void AnimationPreviewSession::SetFact(const std::string& fact, AnimationScenarioValue value)
{
    AnimationScenarioAction action;
    action.Kind = AnimationScenarioActionKind::SetFact;
    action.Fact = fact;
    action.Value = std::move(value);
    Schedule(std::move(action));
}

void AnimationPreviewSession::ClearFact(const std::string& fact)
{
    AnimationScenarioAction action;
    action.Kind = AnimationScenarioActionKind::ClearFact;
    action.Fact = fact;
    Schedule(std::move(action));
}

void AnimationPreviewSession::IssueRequest(AnimationScenarioAction request)
{
    request.Kind = AnimationScenarioActionKind::IssueRequest;
    Schedule(std::move(request));
}

void AnimationPreviewSession::CancelRequest(const std::string& participant, const std::string& intent,
                                            AnimCancelReason reason)
{
    AnimationScenarioAction action;
    action.Kind = AnimationScenarioActionKind::CancelRequest;
    action.Participant = participant;
    action.Intent = intent;
    action.Reason = reason;
    Schedule(std::move(action));
}

void AnimationPreviewSession::Replay()
{
    const AnimTick at = CurrentTick;
    Restart();
    RunTo(at);
}

void AnimationPreviewSession::SetRole(AnimationPreviewRole role)
{
    if (Working.Role == role)
        return;
    Working.Role = role;
    if (Preview != nullptr)
        Replay();
}

void AnimationPreviewSession::SetRecorder(std::string_view verb, bool attached)
{
    const auto it = std::find(Working.Recorders.begin(), Working.Recorders.end(), verb);
    if (attached == (it != Working.Recorders.end()))
        return;
    if (attached)
        Working.Recorders.emplace_back(verb);
    else
        Working.Recorders.erase(it);
    if (Preview != nullptr)
        Replay();
}

bool AnimationPreviewSession::HasRecorder(std::string_view verb) const
{
    return std::find(Working.Recorders.begin(), Working.Recorders.end(), verb) != Working.Recorders.end();
}

VerbAdmission AnimationPreviewSession::Recorder::Invoke(const VerbInvocation& invocation)
{
    if (Session->InvocationSink == nullptr)
        return VerbAdmission::Refused;
    AnimationPreviewInvocation recorded;
    recorded.Tick = invocation.Tick;
    recorded.Verb = Verb;
    if (Session->Bound != nullptr)
        if (const CompiledVerbBinding* binding = Session->Bound->Bindings.Find(invocation.Binding))
            recorded.Binding = binding->KeyText;
    const auto name = [&](EntityId entity) -> std::string {
        if (!entity.IsValid())
            return {};
        if (entity == Session->SubjectEntity)
            return "subject";
        return std::string(Session->ParticipantName(entity));
    };
    recorded.Producer = name(invocation.Producer);
    recorded.Instigator = name(invocation.Instigator);
    const VerbRegistry* verbs = Session->Verbs();
    const VerbDefinition* definition = verbs != nullptr ? verbs->Get(invocation.Verb) : nullptr;
    if (definition != nullptr && invocation.Arguments != nullptr)
        for (std::size_t slot = 0; slot < definition->Arguments.Children.size(); ++slot)
            recorded.Arguments.emplace_back(definition->Arguments.Children[slot].Key,
                                            ValueText(invocation.Arguments->At(slot), Session->Tags()));
    Session->InvocationSink->push_back(std::move(recorded));
    return VerbAdmission::Accepted;
}

bool AnimationPreviewSession::AddParticipant(const std::string& name)
{
    if (name.empty() || Working.HasParticipant(name))
        return false;
    Working.Participants.push_back(name);
    return true;
}

std::optional<AnimationPreviewSession::Refusal> AnimationPreviewSession::ApplyFact(
    AnimationScenarioActionKind kind, const std::string& fact, const AnimationScenarioValue& value,
    InputTable& inputs) const
{
    const int index = Bound->FindSlot(fact);
    if (index < 0)
        return Refusal{ "anim.scenario.unknown_fact",
                        std::format("'{}' is not a fact of '{}'.", fact, Bound->RigPath) };
    const AnimBoundFactSlot& slot = Bound->Slots[static_cast<std::size_t>(index)];
    if (slot.Derivation >= 0)
        return Refusal{ "anim.scenario.derived_fact",
                        std::format("'{}' is derived from other facts and cannot be set.", fact) };
    if (slot.Kind == AnimFactKind::TagSet)
        return Refusal{ "anim.scenario.tagset_input",
                        std::format("'{}' reads the entity's tag container, which scenarios do not "
                                    "drive yet.",
                                    fact) };
    if (kind == AnimationScenarioActionKind::ClearFact)
    {
        inputs.erase(fact);
        return std::nullopt;
    }
    if (!Encode(value, slot.Kind, Preview->TryGetResource<GameplayTagRegistry>()))
    {
        if (slot.Kind == AnimFactKind::Tag && value.Type == AnimationScenarioValue::Kind::Name)
            return Refusal{ "anim.scenario.unknown_tag",
                            std::format("'{}' is not a gameplay tag this project declares.", value.Name) };
        return Refusal{ "anim.scenario.value_kind",
                        std::format("'{}' is a {}, and the scenario gives it {}.", fact,
                                    AnimFactKindName(slot.Kind), ValueKindName(value)) };
    }
    inputs[fact] = value;
    return std::nullopt;
}

AnimationPreviewActionOutcome AnimationPreviewSession::Apply(const AnimationScenarioAction& action,
                                                             std::size_t index, AnimTick tick)
{
    AnimationPreviewActionOutcome outcome;
    outcome.Kind = action.Kind;

    if (action.Kind == AnimationScenarioActionKind::SetFact
        || action.Kind == AnimationScenarioActionKind::ClearFact)
    {
        outcome.Subject = action.Fact;
        if (std::optional<Refusal> refusal = ApplyFact(action.Kind, action.Fact, action.Value, Inputs))
        {
            outcome.Problem = refusal->Message;
            Problem(refusal->Code,
                    ScenarioField(index, action.Kind == AnimationScenarioActionKind::SetFact ? "set" : "clear"),
                    refusal->Message);
        }
        return outcome;
    }

    outcome.Subject = action.Intent;
    const EntityId source = ParticipantEntity(action.Participant);
    if (!source.IsValid())
    {
        outcome.Problem = std::format("'{}' is not a participant.", action.Participant);
        Problem("anim.scenario.unknown_participant", ScenarioField(index, "source"), outcome.Problem);
        return outcome;
    }
    const GameplayTagRegistry* tags = Preview->TryGetResource<GameplayTagRegistry>();
    const GameplayTagId intent = tags != nullptr ? tags->FindTag(action.Intent) : GameplayTagId{};
    if (!intent.IsValid())
    {
        outcome.Problem = std::format("'{}' is not a gameplay tag this project declares.", action.Intent);
        Problem("anim.scenario.unknown_intent",
                ScenarioField(index, action.Kind == AnimationScenarioActionKind::IssueRequest ? "issue" : "cancel"),
                outcome.Problem);
        return outcome;
    }

    if (action.Kind == AnimationScenarioActionKind::CancelRequest)
    {
        // The source's live request for the intent: a stable reference that
        // survives edits which renumber requests.
        const AnimRequestSet* set = Requests();
        const AnimRequest* target = nullptr;
        for (const AnimRequest& request : set->Records)
        {
            if (request.Id.Source == source && request.Intent == intent
                && IsAnimRequestLive(request, tick)
                && (target == nullptr || request.Id.Sequence > target->Id.Sequence))
                target = &request;
        }
        if (target == nullptr)
        {
            outcome.Problem = std::format("'{}' has no live '{}' request to cancel.",
                                          action.Participant, action.Intent);
            return outcome;
        }
        outcome.Request.Id = target->Id;
        outcome.Cancelled = CancelAnimRequest(*Preview, SubjectEntity, target->Id, action.Reason, tick);
        return outcome;
    }

    AnimRequestDesc desc;
    desc.Source = source;
    desc.Intent = intent;
    desc.Layers = action.Layers;
    desc.Lifetime = action.Lifetime;
    desc.FixedTicks = action.FixedTicks;
    const AnimBoundIntent* declared = Bound->FindIntent(intent);
    for (std::size_t p = 0; p < action.Params.size(); ++p)
    {
        const auto& [name, value] = action.Params[p];
        const std::string field = std::format("{}.{}", ScenarioField(index, "params"), name);
        std::size_t slot = 0;
        while (declared != nullptr && slot < declared->Params.size() && declared->Params[slot].Name != name)
            ++slot;
        if (declared == nullptr || slot == declared->Params.size())
        {
            outcome.Problem = std::format("'{}' declares no parameter '{}'.", action.Intent, name);
            Problem("anim.scenario.unknown_param", field, outcome.Problem);
            continue;
        }
        const std::optional<std::uint32_t> encoded =
            Encode(value, ParamKind(declared->Params[slot].Kind), tags);
        if (!encoded)
        {
            outcome.Problem = std::format("'{}' cannot hold {}.", name, ValueKindName(value));
            Problem("anim.scenario.value_kind", field, outcome.Problem);
            continue;
        }
        desc.Params[slot] = *encoded;
    }
    outcome.Request = IssueAnimRequest(*Preview, SubjectEntity, desc, tick);
    return outcome;
}

void AnimationPreviewSession::RunTick(AnimTick tick)
{
    CurrentTick = tick;
    AnimationPreviewTickRecord record;
    record.Tick = tick;

    const auto first = std::lower_bound(
        Working.Actions.begin(), Working.Actions.end(), tick,
        [](const AnimationScenarioAction& action, AnimTick t) { return action.Tick < t; });
    for (auto it = first; it != Working.Actions.end() && it->Tick == tick; ++it)
    {
        const std::size_t index = static_cast<std::size_t>(it - Working.Actions.begin());
        record.Actions.push_back(Apply(*it, index, tick));
    }

    Gather.Gather(*Preview, tick, TickSeconds());
    const DataAssetHandle rig = Preview->TryGet<AnimRig>(SubjectEntity)->Rig;
    Bound = Preview->GetResource<AnimRigBindings>().Resolve(rig, *Preview);

    // Selection and resolution through the same functions the systems run,
    // keeping the verdicts the systems throw away.
    std::vector<std::vector<AnimRuleVerdict>> verdicts;
    if (Bound != nullptr && Bound->Valid)
    {
        AnimSelectorState* selection = Preview->TryGet<AnimSelectorState>(SubjectEntity);
        AnimContentState* content = Preview->TryGet<AnimContentState>(SubjectEntity);
        AnimDecisionLog* log = Preview->TryGet<AnimDecisionLog>(SubjectEntity);
        if (selection != nullptr && !Bound->Selectors.empty())
            SelectAnimEntity(*Preview, SubjectEntity, *Bound, Facts(), *selection, tick, TickSeconds(), log,
                             &verdicts);
        if (content != nullptr)
        {
            ResolveAnimEntity(*Preview, SubjectEntity, *Bound, Facts(), selection, *content, tick, TickSeconds(),
                              log);
            // The production event pass: crossings collected, then offered
            // through the preview's dispatcher, whose only implementations
            // are recorders.
            PendingEvents.clear();
            const AnimFlowState* flows = static_cast<const World&>(*Preview).TryGet<AnimFlowState>(SubjectEntity);
            CollectAnimEvents(SubjectEntity, rig, *Bound, selection, Requests(), flows, *content, tick, TickSeconds(),
                              AnimEventGates{ .Authority = Working.Role == AnimationPreviewRole::Authority,
                                              .Presents = true },
                              PendingEvents, kPreviewEventCapacity, log);
            InvocationSink = &record.Invocations;
            DrainAnimEvents(*Preview, PendingEvents, Dispatcher.get());
            InvocationSink = nullptr;
        }
        for (std::size_t l = 0; l < Bound->Layers.size() && l < kAnimMaxLayers; ++l)
        {
            AnimationPreviewLayerRecord layer;
            if (selection != nullptr)
            {
                layer.Winner = selection->Layers[l].Winner;
                layer.Latch = selection->Layers[l].Latch;
                layer.WeightRule = selection->Layers[l].WeightRule;
            }
            layer.Weight = AnimLayerWeight(*Bound, l, selection);
            if (content != nullptr)
            {
                const AnimLayerContent& playing = content->Layers[l];
                layer.Behavior = playing.Behavior;
                layer.Row = playing.Row;
                layer.Content = playing.Content;
                layer.Clip = playing.Clip;
                layer.TimeSeconds = playing.TimeSeconds;
                layer.ContentComplete = playing.ContentComplete;
            }
            if (const AnimFlowState* flows = static_cast<const World&>(*Preview).TryGet<AnimFlowState>(SubjectEntity))
                layer.Flow = flows->Layers[l];
            if (l < verdicts.size())
                layer.Verdicts = std::move(verdicts[l]);
            record.Layers.push_back(std::move(layer));
        }
    }

    const std::span<const std::uint32_t> facts = Facts();
    record.Facts.assign(facts.begin(), facts.end());
    record.FactsExact = FactsExact();
    if (const AnimRequestSet* set = Requests())
    {
        for (const AnimRequest& request : set->Records)
        {
            if (request.Occupied)
                record.Requests.push_back(request);
        }
    }
    if (const AnimDecisionLog* log = DecisionLog())
    {
        const std::uint64_t fresh = std::min<std::uint64_t>(log->Written - LogWritten, log->Size());
        for (std::size_t i = log->Size() - static_cast<std::size_t>(fresh); i < log->Size(); ++i)
            record.Decisions.push_back(log->At(i));
        LogWritten = log->Written;
    }

    Records.push_back(std::move(record));
    while (Records.size() > kHistoryCapacity)
        Records.pop_front();
}

std::vector<std::uint32_t> AnimationPreviewSession::PreviewNextTick()
{
    if (Preview == nullptr || Bound == nullptr || !Bound->HasFacts)
        return {};

    InputTable next = Inputs;
    for (const AnimationScenarioAction& action : Working.Actions)
    {
        if (action.Tick == NextTick()
            && (action.Kind == AnimationScenarioActionKind::SetFact
                || action.Kind == AnimationScenarioActionKind::ClearFact))
            (void)ApplyFact(action.Kind, action.Fact, action.Value, next);
    }

    const std::span<const std::uint32_t> facts = Facts();
    std::vector<std::uint32_t> values(kAnimFactsLarge, 0u);
    std::copy(facts.begin(), facts.end(), values.begin());
    AnimFactHistory history = *Preview->TryGet<AnimFactHistory>(SubjectEntity);

    ActiveInputs = &next;
    GatherAnimFacts(*Preview, SubjectEntity, *Bound, values, history, NextTick(), TickSeconds());
    ActiveInputs = &Inputs;

    values.resize(Bound->Slots.size());
    return values;
}

std::vector<std::vector<AnimRuleVerdict>> AnimationPreviewSession::ExplainNextTick()
{
    std::vector<std::vector<AnimRuleVerdict>> verdicts;
    const AnimSelectorState* selection = Selection();
    if (Preview == nullptr || Bound == nullptr || !Bound->Valid || selection == nullptr)
        return verdicts;
    const std::vector<std::uint32_t> facts = PreviewNextTick();
    AnimSelectorState copy = *selection;
    SelectAnimEntity(*Preview, SubjectEntity, *Bound, facts, copy, NextTick(), TickSeconds(), nullptr, &verdicts);
    return verdicts;
}

void AnimationPreviewSession::Rebind()
{
    if (Preview == nullptr || !SubjectEntity.IsValid())
        return;
    const AnimRig* rig = Preview->TryGet<AnimRig>(SubjectEntity);
    if (rig != nullptr)
        Bound = Preview->GetResource<AnimRigBindings>().Resolve(rig->Rig, *Preview);
}

std::span<const std::uint32_t> AnimationPreviewSession::Facts() const
{
    if (Preview == nullptr || Bound == nullptr || !SubjectEntity.IsValid())
        return {};
    const std::size_t count = Bound->Slots.size();
    if (const AnimFacts* small = Preview->TryGet<AnimFacts>(SubjectEntity))
        return std::span<const std::uint32_t>(small->Values, std::min(count, kAnimFactsSmall));
    if (const AnimFactsLarge* large = Preview->TryGet<AnimFactsLarge>(SubjectEntity))
        return std::span<const std::uint32_t>(large->Values, std::min(count, kAnimFactsLarge));
    return {};
}

bool AnimationPreviewSession::FactsExact() const
{
    if (Preview == nullptr || Bound == nullptr || !SubjectEntity.IsValid())
        return false;
    const AnimFactHistory* history = Preview->TryGet<AnimFactHistory>(SubjectEntity);
    return history != nullptr && AreAnimDerivedFactsExact(*Bound, *history, CurrentTick, TickSeconds());
}

const AnimRequestSet* AnimationPreviewSession::Requests() const
{
    return Preview != nullptr && SubjectEntity.IsValid() ? Preview->TryGet<AnimRequestSet>(SubjectEntity)
                                                         : nullptr;
}

const AnimSelectorState* AnimationPreviewSession::Selection() const
{
    return Preview != nullptr && SubjectEntity.IsValid() ? Preview->TryGet<AnimSelectorState>(SubjectEntity)
                                                         : nullptr;
}

const AnimContentState* AnimationPreviewSession::Content() const
{
    return Preview != nullptr && SubjectEntity.IsValid() ? Preview->TryGet<AnimContentState>(SubjectEntity)
                                                         : nullptr;
}

const AnimDecisionLog* AnimationPreviewSession::DecisionLog() const
{
    return Preview != nullptr && SubjectEntity.IsValid() ? Preview->TryGet<AnimDecisionLog>(SubjectEntity)
                                                         : nullptr;
}

std::vector<AnimDiagnostic> AnimationPreviewSession::Problems() const
{
    std::vector<AnimDiagnostic> problems;
    if (Bound != nullptr)
        problems = Bound->Diagnostics;
    problems.insert(problems.end(), ScenarioIssues.begin(), ScenarioIssues.end());
    return problems;
}

const AnimationScenarioValue* AnimationPreviewSession::Input(std::string_view fact) const
{
    const auto found = Inputs.find(std::string(fact));
    return found != Inputs.end() ? &found->second : nullptr;
}

EntityId AnimationPreviewSession::ParticipantEntity(std::string_view name) const
{
    for (const auto& [participant, entity] : Participants)
    {
        if (participant == name)
            return entity;
    }
    return EntityId{};
}

std::string_view AnimationPreviewSession::ParticipantName(EntityId entity) const
{
    for (const auto& [participant, id] : Participants)
    {
        if (id == entity)
            return participant;
    }
    return {};
}

const GameplayTagRegistry* AnimationPreviewSession::Tags() const
{
    return Preview != nullptr ? Preview->TryGetResource<GameplayTagRegistry>() : nullptr;
}

const VerbRegistry* AnimationPreviewSession::Verbs() const
{
    return Preview != nullptr ? FindVerbRegistry(*Preview) : nullptr;
}
