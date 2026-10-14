// The ability pass: admission, phases, input edges, guards, commits, cooldowns,
// leases and every way an activation ends, driven through real input records and
// the real dispatchers.

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityVerbs.h>
#include <app/EngineVocabulary.h>
#include <anim/AnimRequestVerbs.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <controller/Aim.h>
#include <controller/LookOrientation.h>
#include <core/json/JsonParser.h>
#include <ecs/Query.h>
#include <effects/EffectVerbs.h>
#include <input/InputActionRegistry.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <input/InputActionVocabulary.h>
#include <physics/PhysicsAuthoredQueries.h>
#include <physics/PhysicsQueries.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <time/ActorTimeSystem.h>
#include <time/SimClock.h>
#include <world/transform/TransformComponents.h>

#include <AllocationCounter.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
constexpr double kTickSeconds = 1.0 / 60.0;

struct RecordingLease
{
    std::vector<std::pair<LeaseStep, LeaseOwner>> Calls;
    bool Refuse = false;

    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        Calls.emplace_back(invocation.Step, invocation.Owner);
        return Refuse && invocation.Step == LeaseStep::Acquire ? VerbAdmission::Refused : VerbAdmission::Accepted;
    }

    [[nodiscard]] std::size_t Count(LeaseStep step) const
    {
        return static_cast<std::size_t>(std::ranges::count(Calls, step, &std::pair<LeaseStep, LeaseOwner>::first));
    }
};

struct RecordingVerb
{
    std::vector<double> Values;
    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        double value = 0.0;
        if (invocation.Arguments != nullptr)
            (void)invocation.Arguments->TryGetFloat(0, value);
        Values.push_back(value);
        return VerbAdmission::Accepted;
    }
};

struct Readiness
{
    bool Ready = true;
};

AuthoredQueryStatus EvaluateReady(const Readiness& readiness, std::span<const AuthoredValue>, AuthoredValue& result)
{
    result = AuthoredValue::Bool(readiness.Ready);
    return AuthoredQueryStatus::Value;
}

std::string Describe(const std::vector<AbilityDiagnostic>& diagnostics)
{
    std::string text;
    for (const AbilityDiagnostic& diagnostic : diagnostics)
        text += diagnostic.Code + " " + diagnostic.ElementKey + " " + diagnostic.Path + ": " + diagnostic.Message + "\n";
    return text;
}

InputActionEdge Press(std::uint8_t action) { return { action, InputEdgeKind::Press }; }
InputActionEdge Release(std::uint8_t action) { return { action, InputEdgeKind::Release }; }
InputActionEdge Interrupt(std::uint8_t action) { return { action, InputEdgeKind::InterruptedRelease }; }

constexpr std::uint8_t kFire = 0;
constexpr std::uint8_t kAlt = 1;

class AbilityRuntime : public ::testing::Test
{
protected:
    AbilityRuntime()
    {
        RegisterAbilityKit(Entities);
        Entities.RegisterComponent<LocalTransform>();
        Entities.RegisterComponent<LookOrientation>();
        Entities.RegisterComponent<InputActionSourceRef>();
        InstallAuthoredVocabulary(Entities);

        GameplayTagRegistry& tags = Entities.GetResource<GameplayTagRegistry>();
        for (const char* name : { "State.Stunned", "State.Dead", "Weapon.Reloading", "Weapon.Firing",
                                  "Weapon.CTX.Mode.Charge", "Cooldown.Charge", "Cooldown.Shot", "weapon.charge",
                                  "cue.charge_glow", "cue.charge_release", "cue.impact", "cue.shot", "Buff.Haste",
                                  "Mode.Alt", "State.Busy" })
            (void)tags.RegisterTag(name);
        AttributeRegistry& attributes = Entities.GetResource<AttributeRegistry>();
        Health = attributes.RegisterAttribute("Health", 0.0f, 100.0f, 100.0f);
        Ammo = attributes.RegisterAttribute("Ammo", 0.0f, 30.0f, 30.0f);
        Energy = attributes.RegisterAttribute("Energy", 0.0f, 100.0f, 100.0f);

        EffectRegistry& effects = Entities.GetResource<EffectRegistry>();
        EffectDefinition ammo{};
        ammo.Modifiers = { { Ammo, ModifierOp::Add, -1.0f } };
        effects.Define("asset://effects/ammo_1.sdata", ammo);
        EffectDefinition damage{};
        damage.Modifiers = { { Health, ModifierOp::Add, -40.0f } };
        effects.Define("asset://effects/charge_damage.sdata", damage);
        EffectDefinition chargeCooldown{};
        chargeCooldown.Duration = EffectDuration::Duration;
        chargeCooldown.DurationSeconds = 0.5f;
        chargeCooldown.GrantedTags = { tags.FindTag("Cooldown.Charge") };
        effects.Define("asset://effects/charge_cooldown.sdata", chargeCooldown);
        EffectDefinition shotCooldown{};
        shotCooldown.Duration = EffectDuration::Duration;
        shotCooldown.DurationSeconds = 5.0f * static_cast<float>(kTickSeconds);
        shotCooldown.GrantedTags = { tags.FindTag("Cooldown.Shot") };
        effects.Define("asset://effects/shot_cooldown.sdata", shotCooldown);

        {
            VerbRegistrationScope verbs(*FindVerbRegistry(Entities), "test");
            VerbDefinition mark;
            mark.Name = "test.mark";
            DataFieldSchema value = MakeDataField(DataFieldKind::Float, "value", "Value");
            value.Default = 0.0;
            value.Required = false;
            mark.Arguments.Children.push_back(std::move(value));
            (void)verbs.Declare(std::move(mark));
            VerbDefinition hold;
            hold.Name = "test.hold";
            hold.Kind = VerbKind::Lease;
            (void)verbs.Declare(std::move(hold));
            AuthoredQueryRegistrationScope queries(*FindAuthoredQueryRegistry(Entities), "test");
            AuthoredQueryDefinition ready;
            ready.Name = "test.ready";
            ready.Result.Kind = DataFieldKind::Bool;
            (void)queries.Declare(std::move(ready));
            EXPECT_TRUE(CommitTogether(verbs, queries));
        }
        EXPECT_TRUE(DeclareEngineVocabulary(Entities));

        Actions.Rebuild(std::vector<InputActionDefinition>{ { .Name = "Input.PrimaryFire" }, { .Name = "Input.AltFire" } });
        Entities.SetResource(InputActionVocabulary{ &Actions, 1 });
        Entities.AddResource<InputActionState>().Configure(2);

        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(Entities));
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(Entities));
        const VerbRegistry& catalog = Verbs->Registry();
        Tokens.push_back(Verbs->Bind(catalog.Find("test.mark"), Marks));
        Tokens.push_back(Verbs->Bind(catalog.Find("test.hold"), Held));
        Tokens.push_back(Verbs->Bind(catalog.Find(kAnimHoldLease), Posed));
        Tokens.push_back(Verbs->Bind<&InvokeEffectApply>(catalog.Find(kEffectApplyVerb), Effects));
        Tokens.push_back(Verbs->Bind<&InvokeEffectHold>(catalog.Find(kEffectHoldLease), Effects));
        Tokens.push_back(Verbs->Bind<&InvokeAbilityActivate>(catalog.Find(kAbilityActivateVerb), AbilityVerbs));
        Tokens.push_back(Verbs->Bind<&InvokeAbilityCancel>(catalog.Find(kAbilityCancelVerb), AbilityVerbs));
        QueryTokens.push_back(Queries->Bind<&EvaluateReady>(Queries->Registry().Find("test.ready"), std::as_const(Ready)));
        QueryTokens.push_back(Queries->Bind<&EvaluatePhysicsTrace>(Queries->Registry().Find(kPhysicsTraceQuery),
                                                                   std::as_const(Traces)));
        Host = AbilityHost{ .Verbs = Verbs.get(), .Queries = Queries.get() };
    }

    AbilityId DefineData(std::string_view path, const JsonValue& data)
    {
        AbilityDeclaration declaration;
        std::vector<AssetRef> dependencies;
        std::vector<AbilityDiagnostic> diagnostics;
        EXPECT_TRUE(CompileAbilityDeclaration(data, declaration, dependencies, diagnostics)) << Describe(diagnostics);
        AbilityDefinition definition;
        EXPECT_TRUE(ResolveAbilityDeclaration(declaration, path, MakeAbilityResolveEnvironment(Entities), definition,
                                              diagnostics))
            << Describe(diagnostics);
        return Entities.GetResource<AbilityRegistry>().Define(path, std::move(definition));
    }

    AbilityId Define(std::string_view path, std::string_view json)
    {
        const std::optional<JsonValue> data = JsonParse(json);
        EXPECT_TRUE(data.has_value()) << json;
        return DefineData(path, *data);
    }

    AbilityId DefineCharge()
    {
        const std::optional<JsonValue> file = JsonParseFile(std::filesystem::path(SENCHA_REPO_ROOT)
                                                            / "test/fixtures/abilities/assets/abilities/charge_release.sdata");
        EXPECT_TRUE(file.has_value());
        return DefineData("asset://abilities/charge_release.sdata", *file->Find("data"));
    }

    EntityId Actor()
    {
        const EntityId actor = Entities.CreateEntity();
        Entities.AddComponent<LocalTransform>(actor, LocalTransform{});
        Entities.AddComponent<LookOrientation>(actor, LookOrientation{});
        Entities.AddComponent<InputActionSourceRef>(actor, InputActionSourceRef{});
        Entities.AddComponent<AttributeSet>(actor);
        AttributeSet& set = *Entities.TryGet<AttributeSet>(actor);
        set.Add(Health, 100.0f);
        set.Add(Ammo, 3.0f);
        set.Add(Energy, 10.0f);
        Entities.AddComponent<AbilityGrants>(actor, AbilityGrants{});
        return actor;
    }

    AbilityGrantId Grant(EntityId actor, AbilityId ability, AbilityBindingDecl over = {}, std::string key = "g")
    {
        AbilityGrantEntries& entries = Entities.GetResource<AbilityGrantEntries>();
        const std::uint16_t entry = entries.Intern({ std::move(key), ability, std::move(over) });
        return GrantAbility(*Entities.TryGet<AbilityGrants>(actor), ability, AbilityGrantKind::Authored, entry);
    }

    // One fixed tick in the schedule's order: the actor clock, the ability pass,
    // attribute resolve, effect lifetime.
    void Step(std::vector<InputActionEdge> edges = {})
    {
        InputActionState& state = Entities.GetResource<InputActionState>();
        std::span<InputActionValue> values = state.BeginTick(Now);
        for (const InputActionEdge& edge : edges)
        {
            HeldActions[edge.Action] = edge.Kind == InputEdgeKind::Press;
            (void)state.TickEdgeStorage().Append(edge);
        }
        for (std::size_t a = 0; a < values.size(); ++a)
            values[a].Flags = HeldActions[a] ? InputActionFlags::Held : InputActionFlags::None;
        AdvanceActorTime(Entities);
        RunAbilityPass(Entities, Host, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now }, nullptr, Settings);
        ResolveAttributesWithEffects(Entities);
        TickEffects(Entities);
        ++Now;
    }

    void Steps(int count)
    {
        for (int i = 0; i < count; ++i)
            Step();
    }

    [[nodiscard]] std::vector<AbilityTraceRecord> Traced(AbilityTraceEvent event)
    {
        std::vector<AbilityTraceRecord> found;
        Entities.GetResource<AbilityTrace>().Records().ForEach([&](const AbilityTraceRecord& record) {
            if (record.Event == event)
                found.push_back(record);
        });
        return found;
    }

    [[nodiscard]] const AbilityRecord* LiveRecord(EntityId actor, AbilityId ability)
    {
        for (const AbilityRecord& record : Entities.TryGet<ActiveAbilities>(actor)->Records)
            if (record.State == AbilityRecordState::Live && record.Key.Ability == ability)
                return &record;
        return nullptr;
    }

    [[nodiscard]] std::size_t LiveCount(EntityId actor)
    {
        const auto& records = Entities.TryGet<ActiveAbilities>(actor)->Records;
        return static_cast<std::size_t>(std::ranges::count(records, AbilityRecordState::Live, &AbilityRecord::State));
    }

    [[nodiscard]] float Base(EntityId entity, AttributeId attribute)
    {
        return Entities.TryGet<AttributeSet>(entity)->GetBase(attribute);
    }

    [[nodiscard]] GameplayTagId Tag(std::string_view name)
    {
        return Entities.GetResource<GameplayTagRegistry>().FindTag(name);
    }

    [[nodiscard]] bool HasTag(EntityId entity, std::string_view name)
    {
        return Entities.TryGet<GameplayTagContainer>(entity)->StackCount(Tag(name)) > 0;
    }

    // A struck body three meters along the actor's aim.
    EntityId Target(EntityId actor, bool withAttributes)
    {
        const EntityId target = Entities.CreateEntity();
        if (withAttributes)
        {
            Entities.AddComponent<AttributeSet>(target);
            Entities.TryGet<AttributeSet>(target)->Add(Health, 100.0f);
        }
        BodyDesc box;
        box.Shape = CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f));
        box.Position = AimOf(Entities, actor).Direction * 3.0f;
        box.Motion = BodyMotion::Static;
        box.Layer = CollisionLayer::Static;
        box.UserData = PackEntity(target);
        (void)Physics.AddBody(box);
        return target;
    }

    void Wall(EntityId actor)
    {
        BodyDesc box;
        box.Shape = CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f));
        box.Position = AimOf(Entities, actor).Direction * 3.0f;
        box.Motion = BodyMotion::Static;
        box.Layer = CollisionLayer::Static;
        (void)Physics.AddBody(box);
    }

    World Entities;
    AttributeId Health;
    AttributeId Ammo;
    AttributeId Energy;
    InputActionRegistry Actions;
    bool HeldActions[2] = {};
    std::uint64_t Now = 0;
    AbilitySettings Settings;

    PhysicsWorld Physics;
    PhysicsQueries Traces{ Physics };
    FixedSimulationLoop Clock{ kTickSeconds };
    EffectOperations Effects{ Entities, Clock };
    AbilityOperations AbilityVerbs{ Entities };
    RecordingVerb Marks;
    RecordingLease Held;
    RecordingLease Posed;
    Readiness Ready;
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::vector<VerbBindingToken> Tokens;
    std::vector<AuthoredQueryBindingToken> QueryTokens;
    AbilityHost Host;
};

constexpr std::string_view kInstant = R"({
    "input_default": { "action": "Input.PrimaryFire" },
    "phases": [ { "key": "p_fire",
        "enter": [ { "key": "o_mark", "op": "invoke", "verb": "test.mark", "arguments": { "value": { "const": 1 } } } ],
        "then": [ { "key": "t_end", "to": "end" } ] } ] })";

// Charges while held; a release after six ticks fires, an earlier one recovers.
constexpr std::string_view kCharge = R"({
    "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
    "phases": [
      { "key": "p_charge", "then": [
          { "key": "t_fire", "on": "release", "when": { "key": "w_min", "phase_ticks": { "at_least": 6 } }, "to": "p_fire" },
          { "key": "t_early", "on": "release", "to": "p_recover" } ] },
      { "key": "p_fire", "enter": [ { "key": "o_mark", "op": "invoke", "verb": "test.mark" } ],
        "then": [ { "key": "t_recover", "to": "p_recover" } ] },
      { "key": "p_recover", "then": [ { "key": "t_done", "on": "timeout", "ticks": 20, "to": "end" } ] } ] })";

// Fires every four ticks while held, starting on the press.
constexpr std::string_view kAutomatic = R"({
    "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
    "costs": [ { "key": "g_round", "repeat": "per_pulse", "effects": [ { "data": "asset://effects/ammo_1.sdata" } ] } ],
    "cooldown": { "effect": { "data": "asset://effects/shot_cooldown.sdata" }, "start": "on_end" },
    "phases": [ { "key": "p_fire",
        "while": { "every": 4, "ops": [
            { "key": "o_pay", "op": "commit", "groups": ["g_round"] },
            { "key": "o_mark", "op": "invoke", "verb": "test.mark", "arguments": { "value": { "port": "pulse" } } } ] },
        "then": [ { "key": "t_stop", "on": "release", "to": "end" } ] } ] })";
}

TEST_F(AbilityRuntime, ThreePressesOfAnInstantAbilityActivateThreeTimes)
{
    const AbilityId primary = Define("asset://abilities/primary.sdata", kInstant);
    const EntityId actor = Actor();
    Grant(actor, primary);

    Step({ Press(kFire), Release(kFire), Press(kFire), Release(kFire), Press(kFire) });

    EXPECT_EQ(Marks.Values.size(), 3u);
    const std::vector<AbilityTraceRecord> started = Traced(AbilityTraceEvent::Started);
    ASSERT_EQ(started.size(), 3u);
    EXPECT_EQ(started[0].Key.Origin, 0u);
    EXPECT_EQ(started[1].Key.Origin, 2u);
    EXPECT_EQ(started[2].Key.Origin, 4u) << "keyed by the press's place in the tick, not by a count of starts";
    EXPECT_EQ(LiveCount(actor), 0u);
}

TEST_F(AbilityRuntime, PressesBeyondALiveInstanceAreRefusedAndConsumed)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    Grant(actor, charge);

    Step({ Press(kFire), Release(kFire), Press(kFire), Release(kFire), Press(kFire) });

    const std::vector<AbilityTraceRecord> refused = Traced(AbilityTraceEvent::Refused);
    ASSERT_EQ(refused.size(), 2u);
    EXPECT_EQ(refused[0].Refusal, AbilityRefusal::Instances);
    EXPECT_EQ(Traced(AbilityTraceEvent::Blocked).size(), 2u) << "an exclusive refusal consumes its press";
    const AbilityRecord* record = LiveRecord(actor, charge);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Phase, 2u) << "the early release recovers";
    EXPECT_TRUE(Marks.Values.empty());
}

TEST_F(AbilityRuntime, ReleaseIsProcessedBeforeTheSameTicksPulse)
{
    const AbilityId automatic = Define("asset://abilities/automatic.sdata", kAutomatic);
    const EntityId actor = Actor();
    Grant(actor, automatic);

    Step({ Press(kFire) });
    Steps(3);
    ASSERT_EQ(Marks.Values.size(), 1u);
    Step({ Release(kFire) });
    EXPECT_EQ(Marks.Values.size(), 1u) << "the fourth tick's pulse never runs: the release ended it first";
    EXPECT_EQ(LiveCount(actor), 0u);
}

TEST_F(AbilityRuntime, GenuinePressThenInterruptCompletesAnInstantAndCancelsACharge)
{
    const AbilityId primary = Define("asset://abilities/primary.sdata", kInstant);
    AbilityBindingDecl alt;
    alt.Action = "Input.AltFire";
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    Grant(actor, primary);
    Grant(actor, charge, alt, "g_charge");

    Step({ Press(kFire), Interrupt(kFire), Press(kAlt) });
    Steps(10);
    Step({ Interrupt(kAlt) });

    EXPECT_EQ(Marks.Values.size(), 1u) << "the instant ran to the end on its press";
    const std::vector<AbilityTraceRecord> ended = Traced(AbilityTraceEvent::Ended);
    ASSERT_EQ(ended.size(), 2u);
    EXPECT_EQ(ended[1].EndReason, AbilityEndReason::InputInterrupted);
    EXPECT_EQ(ended[1].Phase, 0u) << "the charge never fired";
}

TEST_F(AbilityRuntime, ChargeCapturesProgressAndScalesDamageOnAHit)
{
    const AbilityId charge = DefineCharge();
    const EntityId actor = Actor();
    Entities.TryGet<GameplayTagContainer>(actor)->Grant(Tag("Weapon.CTX.Mode.Charge"));
    Grant(actor, charge);
    const EntityId target = Target(actor, true);

    Step({ Press(kFire) });
    EXPECT_EQ(Posed.Count(LeaseStep::Acquire), 1u) << "the charge pose is held";
    EXPECT_TRUE(HasTag(actor, "Weapon.Firing"));
    Steps(29);
    Step({ Release(kFire) });

    EXPECT_EQ(Posed.Count(LeaseStep::Release), 1u);
    EXPECT_FLOAT_EQ(Base(actor, Ammo), 2.0f) << "one commit";
    EXPECT_FLOAT_EQ(Base(target, Health), 80.0f) << "-40 at half charge";
    EXPECT_TRUE(HasTag(actor, "Cooldown.Charge")) << "the cooldown starts on the commit";
    const std::vector<AbilityTraceRecord> cues = Traced(AbilityTraceEvent::Cue);
    ASSERT_EQ(cues.size(), 2u) << "release and impact";
    EXPECT_EQ(cues[1].Cue, Tag("cue.impact"));
}

TEST_F(AbilityRuntime, EarlyReleaseRecoversWithoutCost)
{
    const AbilityId charge = DefineCharge();
    const EntityId actor = Actor();
    Entities.TryGet<GameplayTagContainer>(actor)->Grant(Tag("Weapon.CTX.Mode.Charge"));
    Grant(actor, charge);

    Step({ Press(kFire) });
    Steps(3);
    Step({ Release(kFire) });

    EXPECT_FLOAT_EQ(Base(actor, Ammo), 3.0f);
    EXPECT_FALSE(HasTag(actor, "Cooldown.Charge"));
    ASSERT_NE(LiveRecord(actor, charge), nullptr);
    EXPECT_EQ(LiveRecord(actor, charge)->Phase, 2u);
    Steps(19);
    ASSERT_NE(LiveRecord(actor, charge), nullptr);
    Step();
    EXPECT_EQ(LiveRecord(actor, charge), nullptr) << "Recover lasts twenty ticks";
}

TEST_F(AbilityRuntime, MissSkipsDamageAndImpact)
{
    const AbilityId charge = DefineCharge();
    const EntityId actor = Actor();
    Entities.TryGet<GameplayTagContainer>(actor)->Grant(Tag("Weapon.CTX.Mode.Charge"));
    Grant(actor, charge);

    Step({ Press(kFire) });
    Steps(10);
    Step({ Release(kFire) });

    const std::vector<AbilityTraceRecord> cues = Traced(AbilityTraceEvent::Cue);
    ASSERT_EQ(cues.size(), 1u) << "the release presents; nothing was struck";
    EXPECT_EQ(cues[0].Cue, Tag("cue.charge_release"));
    EXPECT_EQ(Traced(AbilityTraceEvent::Skipped).size(), 2u) << "damage and impact both skipped";
}

TEST_F(AbilityRuntime, WorldSurfaceHitShowsImpactWithoutDamage)
{
    const AbilityId charge = DefineCharge();
    const EntityId actor = Actor();
    Entities.TryGet<GameplayTagContainer>(actor)->Grant(Tag("Weapon.CTX.Mode.Charge"));
    Grant(actor, charge);
    Wall(actor);

    Step({ Press(kFire) });
    Steps(10);
    Step({ Release(kFire) });

    const std::vector<AbilityTraceRecord> cues = Traced(AbilityTraceEvent::Cue);
    ASSERT_EQ(cues.size(), 2u);
    EXPECT_EQ(cues[1].Cue, Tag("cue.impact"));
    EXPECT_NEAR(cues[1].At.Z, -2.5f, 0.05f) << "at the struck face";
    const std::vector<AbilityTraceRecord> skipped = Traced(AbilityTraceEvent::Skipped);
    ASSERT_EQ(skipped.size(), 1u);
}

TEST_F(AbilityRuntime, HitOnEntityWithoutAttributesIsNotDamaged)
{
    const AbilityId charge = DefineCharge();
    const EntityId actor = Actor();
    Entities.TryGet<GameplayTagContainer>(actor)->Grant(Tag("Weapon.CTX.Mode.Charge"));
    Grant(actor, charge);
    const EntityId prop = Target(actor, false);

    Step({ Press(kFire) });
    Steps(10);
    Step({ Release(kFire) });

    EXPECT_FALSE(Entities.HasComponent<AttributeSet>(prop));
    EXPECT_EQ(Traced(AbilityTraceEvent::Skipped).size(), 1u) << "the recipient guard";
}

TEST_F(AbilityRuntime, ContinuationFailureEndsWithItsKey)
{
    const AbilityId charge = DefineCharge();
    const EntityId actor = Actor();
    GameplayTagContainer& tags = *Entities.TryGet<GameplayTagContainer>(actor);
    tags.Grant(Tag("Weapon.CTX.Mode.Charge"));
    Grant(actor, charge);

    Step({ Press(kFire) });
    Steps(3);
    tags.Revoke(Tag("Weapon.CTX.Mode.Charge"));
    Step();

    const std::vector<AbilityTraceRecord> ended = Traced(AbilityTraceEvent::Ended);
    ASSERT_EQ(ended.size(), 1u);
    EXPECT_EQ(ended[0].EndReason, AbilityEndReason::ContinuationFailed);
    const AbilityDefinition& definition = *Entities.GetResource<AbilityRegistry>().Get(charge);
    EXPECT_EQ(definition.Conditions[ended[0].Element].Key, "cont_mode");
    EXPECT_EQ(Posed.Count(LeaseStep::Release), 1u);
    EXPECT_FALSE(HasTag(actor, "Weapon.Firing"));
}

TEST_F(AbilityRuntime, ActivationConditionsAreNotContinuation)
{
    const AbilityId charge = DefineCharge();
    const EntityId actor = Actor();
    GameplayTagContainer& tags = *Entities.TryGet<GameplayTagContainer>(actor);
    tags.Grant(Tag("Weapon.CTX.Mode.Charge"));
    Grant(actor, charge);

    Step({ Press(kFire) });
    tags.Grant(Tag("Weapon.Reloading"));
    Steps(3);
    EXPECT_NE(LiveRecord(actor, charge), nullptr) << "act_free only gates the start";
}

TEST_F(AbilityRuntime, ACancelConditionEndsTheAttackAndNotTheBuff)
{
    const AbilityId attack = Define("asset://abilities/attack.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "cancel_on": [ { "key": "x_stun", "tags": { "any": ["State.Stunned"] } } ],
        "phases": [ { "key": "p_swing", "then": [ { "key": "t_done", "on": "timeout", "ticks": 30, "to": "end" } ] } ] })");
    const AbilityId buff = Define("asset://abilities/buff.sdata", R"({
        "input_default": { "action": "Input.AltFire" },
        "phases": [ { "key": "p_on", "then": [ { "key": "t_done", "on": "timeout", "ticks": 30, "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, attack);
    Grant(actor, buff, {}, "g_buff");

    Step({ Press(kFire), Press(kAlt) });
    Entities.TryGet<GameplayTagContainer>(actor)->Grant(Tag("State.Stunned"));
    Step();

    EXPECT_EQ(LiveRecord(actor, attack), nullptr);
    EXPECT_NE(LiveRecord(actor, buff), nullptr);
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).front().EndReason, AbilityEndReason::CancelTriggered);
}

TEST_F(AbilityRuntime, CommitAggregatesCostsOnOneAttributeAndAFailedCommitAppliesNothing)
{
    const AbilityId burst = Define("asset://abilities/burst.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "costs": [ { "key": "g_a", "effects": [ { "data": "asset://effects/ammo_1.sdata" },
                                                { "data": "asset://effects/ammo_1.sdata" } ] },
                   { "key": "g_b", "effects": [ { "data": "asset://effects/ammo_1.sdata" },
                                                { "data": "asset://effects/ammo_1.sdata" } ] } ],
        "cooldown": { "effect": { "data": "asset://effects/charge_cooldown.sdata" }, "start": "on_commit", "group": "g_a" },
        "phases": [ { "key": "p_fire",
            "enter": [ { "key": "o_pay", "op": "commit", "groups": ["g_a", "g_b"] },
                       { "key": "o_mark", "op": "invoke", "verb": "test.mark" } ],
            "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, burst);

    Step({ Press(kFire) });

    EXPECT_FLOAT_EQ(Base(actor, Ammo), 3.0f) << "four of three: nothing is paid";
    EXPECT_FALSE(HasTag(actor, "Cooldown.Charge")) << "nor the cooldown";
    EXPECT_TRUE(Marks.Values.empty()) << "and nothing after the commit runs";
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).front().EndReason, AbilityEndReason::CommitFailed);
}

TEST_F(AbilityRuntime, OnceGroupPaysOnceAndPerPulseGroupPaysEachPulse)
{
    const AbilityId twice = Define("asset://abilities/twice.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "costs": [ { "key": "g_shot", "effects": [ { "data": "asset://effects/ammo_1.sdata" } ] } ],
        "phases": [
          { "key": "p_a", "enter": [ { "key": "o_1", "op": "commit", "groups": ["g_shot"] } ], "then": [ { "key": "t_1", "to": "p_b" } ] },
          { "key": "p_b", "enter": [ { "key": "o_2", "op": "commit", "groups": ["g_shot"] } ], "then": [ { "key": "t_2", "to": "end" } ] } ] })");
    const AbilityId automatic = Define("asset://abilities/automatic.sdata", kAutomatic);
    const EntityId once = Actor();
    Grant(once, twice);
    const EntityId pulses = Actor();
    Grant(pulses, automatic);

    Step({ Press(kFire) });
    EXPECT_FLOAT_EQ(Base(once, Ammo), 2.0f);
    EXPECT_EQ(Traced(AbilityTraceEvent::AlreadyCommitted).size(), 1u);

    Steps(8);
    EXPECT_FLOAT_EQ(Base(pulses, Ammo), 0.0f) << "pulses on ticks 0, 4 and 8";
}

TEST_F(AbilityRuntime, AnAutomaticStartsItsCooldownOnceAtTheEnd)
{
    const AbilityId automatic = Define("asset://abilities/automatic.sdata", kAutomatic);
    const EntityId actor = Actor();
    Grant(actor, automatic);

    Step({ Press(kFire) });
    Steps(8);
    EXPECT_FALSE(HasTag(actor, "Cooldown.Shot"));
    Step({ Release(kFire) });
    EXPECT_TRUE(HasTag(actor, "Cooldown.Shot"));
    std::size_t cooldowns = 0;
    Query<Read<ActiveEffect>> effects(Entities);
    effects.ForEachChunk([&](auto& view) { cooldowns += view.Count(); });
    EXPECT_EQ(cooldowns, 1u);
}

TEST_F(AbilityRuntime, ACooldownBlocksTheTickBeforeItsEndAndAdmitsOnIt)
{
    const AbilityId shot = Define("asset://abilities/shot.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "activation": [ { "key": "act_ready", "tags": { "none": ["Cooldown.Shot"] } } ],
        "cooldown": { "effect": { "data": "asset://effects/shot_cooldown.sdata" }, "start": "on_start" },
        "phases": [ { "key": "p_fire", "enter": [ { "key": "o_mark", "op": "invoke", "verb": "test.mark" } ],
            "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, shot);

    Step({ Press(kFire), Release(kFire) });
    Steps(3);
    Step({ Press(kFire), Release(kFire) });
    EXPECT_EQ(Marks.Values.size(), 1u) << "tick N+4 of a five-tick cooldown is still blocked";
    Step({ Press(kFire), Release(kFire) });
    EXPECT_EQ(Marks.Values.size(), 2u) << "tick N+5 admits";
}

TEST_F(AbilityRuntime, AFailedCostIsRefusedAtAdmission)
{
    const AbilityId spend = Define("asset://abilities/spend.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "costs": [ { "key": "g_start", "when": "start", "effects": [ { "data": "asset://effects/ammo_1.sdata" } ] } ],
        "phases": [ { "key": "p_a", "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, spend);
    Entities.TryGet<AttributeSet>(actor)->SetBase(Ammo, 0.0f);
    Step({ Press(kFire) });
    ASSERT_EQ(Traced(AbilityTraceEvent::Refused).size(), 1u);
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused)[0].Refusal, AbilityRefusal::CostFailed);

    // A cost that stops being an Instant Add fails closed rather than guessing.
    EffectDefinition lasting{};
    lasting.Duration = EffectDuration::Infinite;
    lasting.Modifiers = { { Ammo, ModifierOp::Add, -1.0f } };
    Entities.GetResource<EffectRegistry>().Define("asset://effects/ammo_1.sdata", lasting);
    Entities.TryGet<AttributeSet>(actor)->SetBase(Ammo, 3.0f);
    Step({ Press(kFire) });
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused).size(), 2u);
    EXPECT_FLOAT_EQ(Base(actor, Ammo), 3.0f);
}

TEST_F(AbilityRuntime, StepBudgetEndsTheRecord)
{
    std::string phases;
    for (int i = 0; i < 12; ++i)
        phases += std::string(i ? "," : "") + "{ \"key\": \"p_" + std::to_string(i) + "\", \"then\": [ { \"key\": \"t_"
            + std::to_string(i) + "\", \"to\": \"" + (i < 11 ? "p_" + std::to_string(i + 1) : std::string("end"))
            + "\" } ] }";
    const AbilityId chain = Define("asset://abilities/chain.sdata",
                                   R"({ "input_default": { "action": "Input.PrimaryFire" }, "phases": [ )" + phases + " ] }");
    const EntityId actor = Actor();
    Grant(actor, chain);
    Step({ Press(kFire) });
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).front().EndReason, AbilityEndReason::StepBudget);
}

TEST_F(AbilityRuntime, LingeringRecordsDoNotCountAsInstancesAndLeaveAfterTheirWindow)
{
    const AbilityId primary = Define("asset://abilities/primary.sdata", kInstant);
    const EntityId actor = Actor();
    Grant(actor, primary);

    Step({ Press(kFire) });
    Step({ Release(kFire), Press(kFire) });
    EXPECT_EQ(Marks.Values.size(), 2u);
    const auto lingering = [&] {
        const auto& records = Entities.TryGet<ActiveAbilities>(actor)->Records;
        return std::ranges::count(records, AbilityRecordState::Lingering, &AbilityRecord::State);
    };
    EXPECT_EQ(lingering(), 2);
    Steps(static_cast<int>(Settings.LingerTicks) - 1);
    EXPECT_EQ(lingering(), 1);
    Step();
    EXPECT_EQ(lingering(), 0);
}

TEST_F(AbilityRuntime, AModuleQueryGatesAnAbilityAndAModuleLeaseIsHeldAndReleasedInReverse)
{
    const AbilityId gated = Define("asset://abilities/gated.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
        "activation": [ { "key": "act_ready", "query": { "name": "test.ready" } } ],
        "phases": [ { "key": "p_hold",
            "hold": [ { "key": "o_first", "lease": "test.hold" }, { "key": "o_second", "lease": "test.hold" } ],
            "then": [ { "key": "t_end", "on": "release", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, gated);

    Ready.Ready = false;
    Step({ Press(kFire) });
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused).front().Refusal, AbilityRefusal::ActivationFailed);
    EXPECT_TRUE(Held.Calls.empty());

    Ready.Ready = true;
    Step({ Release(kFire), Press(kFire) });
    Step({ Release(kFire) });
    ASSERT_EQ(Held.Calls.size(), 4u);
    EXPECT_EQ(Held.Calls[2].first, LeaseStep::Release);
    EXPECT_EQ(Held.Calls[2].second, Held.Calls[1].second) << "the newest hold is let go first";
    EXPECT_EQ(Held.Calls[3].second, Held.Calls[0].second);
    EXPECT_NE(Held.Calls[0].second, Held.Calls[1].second) << "each op holds under its own key";
}

TEST_F(AbilityRuntime, ARefusedLeaseEndsTheActivation)
{
    const AbilityId gated = Define("asset://abilities/held.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "phases": [ { "key": "p_hold", "hold": [ { "key": "o_hold", "lease": "test.hold" } ],
            "enter": [ { "key": "o_mark", "op": "invoke", "verb": "test.mark" } ],
            "then": [ { "key": "t_end", "on": "timeout", "ticks": 5, "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, gated);
    Held.Refuse = true;
    Step({ Press(kFire) });
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).front().EndReason, AbilityEndReason::LeaseRefused);
    EXPECT_TRUE(Marks.Values.empty()) << "nothing after the refused hold runs";
}

TEST_F(AbilityRuntime, AFifthActivationIsRefusedWithReason)
{
    const AbilityId waiting = Define("asset://abilities/wait.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" }, "instances": 4,
        "phases": [ { "key": "p_wait", "then": [ { "key": "t_end", "on": "timeout", "ticks": 50, "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, waiting);
    Grant(actor, waiting, { .Action = "Input.AltFire" }, "g_alt");
    Step({ Press(kFire), Release(kFire), Press(kFire), Release(kFire), Press(kFire), Release(kFire), Press(kFire),
           Release(kFire), Press(kAlt) });
    EXPECT_EQ(LiveCount(actor), 4u);
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused).front().Refusal, AbilityRefusal::Instances);
}

TEST_F(AbilityRuntime, BindingsResolveByPriorityConsumeAndObserve)
{
    const AbilityId high = Define("asset://abilities/high.sdata", kInstant);
    const AbilityId low = Define("asset://abilities/low.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "phases": [ { "key": "p_fire", "enter": [ { "key": "o_mark", "op": "invoke", "verb": "test.mark",
            "arguments": { "value": { "const": 2 } } } ], "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const AbilityId watcher = Define("asset://abilities/watch.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire", "consume": "observe" },
        "phases": [ { "key": "p_fire", "enter": [ { "key": "o_mark", "op": "invoke", "verb": "test.mark",
            "arguments": { "value": { "const": 3 } } } ], "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, low, { .Priority = 10 }, "g_low");
    Grant(actor, high, { .Priority = 20 }, "g_high");
    Grant(actor, watcher, {}, "g_watch");

    Step({ Press(kFire) });
    EXPECT_EQ(Marks.Values, (std::vector<double>{ 1.0, 3.0 })) << "the higher exclusive, then the observer";

    // Blocked by default; a fall-through binding lets the next one try.
    Entities.TryGet<GameplayTagContainer>(actor)->Grant(Tag("State.Busy"));
    const AbilityId busy = Define("asset://abilities/high.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "activation": [ { "key": "act_free", "tags": { "none": ["State.Busy"] } } ],
        "phases": [ { "key": "p_fire", "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    ASSERT_EQ(busy, high);
    Marks.Values.clear();
    Step({ Release(kFire), Press(kFire) });
    EXPECT_EQ(Marks.Values, (std::vector<double>{ 3.0 }));
    EXPECT_EQ(Traced(AbilityTraceEvent::Blocked).size(), 1u);

    const AbilityId fallThrough = Define("asset://abilities/high.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire", "when_blocked": "fall_through" },
        "activation": [ { "key": "act_free", "tags": { "none": ["State.Busy"] } } ],
        "phases": [ { "key": "p_fire", "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    ASSERT_EQ(fallThrough, high);
    Marks.Values.clear();
    Step({ Release(kFire), Press(kFire) });
    EXPECT_EQ(Marks.Values, (std::vector<double>{ 2.0, 3.0 }));
}

TEST_F(AbilityRuntime, EqualPriorityExclusivesStartNothingAndTrace)
{
    const AbilityId a = Define("asset://abilities/a.sdata", kInstant);
    const AbilityId b = Define("asset://abilities/b.sdata", kInstant);
    const EntityId actor = Actor();
    Grant(actor, a, {}, "g_a");
    Grant(actor, b, {}, "g_b");
    Step({ Press(kFire) });
    EXPECT_TRUE(Marks.Values.empty());
    EXPECT_EQ(Traced(AbilityTraceEvent::BindingAmbiguous).size(), 1u);
}

TEST_F(AbilityRuntime, OneAbilityTwoPawnsTwoActions)
{
    const AbilityId primary = Define("asset://abilities/primary.sdata", kInstant);
    const EntityId first = Actor();
    const EntityId second = Actor();
    Grant(first, primary);
    Grant(second, primary, { .Action = "Input.AltFire" }, "g_alt");
    Step({ Press(kFire) });
    EXPECT_EQ(Marks.Values.size(), 1u) << "only the pawn bound to the pressed action";
    Step({ Release(kFire), Press(kAlt) });
    EXPECT_EQ(Marks.Values.size(), 2u);
}

TEST_F(AbilityRuntime, AReleaseReachesTheActivationItsActionStartedAfterAModeSwitch)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    AbilityBindingDecl inAlt;
    inAlt.Context = AbilityTagQueryDecl{ .All = {}, .Any = {}, .None = { "Mode.Alt" } };
    Grant(actor, charge, inAlt);

    Step({ Press(kFire) });
    Steps(10);
    Entities.TryGet<GameplayTagContainer>(actor)->Grant(Tag("Mode.Alt"));
    Step({ Release(kFire) });
    EXPECT_EQ(Marks.Values.size(), 1u) << "the context gates starts, not the release of one";
}

TEST_F(AbilityRuntime, RevokingOneGrantEndsOnlyItsActivations)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    const AbilityGrantId first = Grant(actor, charge, {}, "g_1");
    Grant(actor, charge, { .Action = "Input.AltFire" }, "g_2");
    Step({ Press(kFire), Press(kAlt) });
    EXPECT_EQ(LiveCount(actor), 1u) << "instances: 1 across both grants";
    ASSERT_TRUE(RevokeAbilityGrant(*Entities.TryGet<AbilityGrants>(actor), first));
    Step();
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).front().EndReason, AbilityEndReason::GrantRevoked);
    Step({ Release(kFire), Release(kAlt), Press(kAlt) });
    EXPECT_EQ(LiveCount(actor), 1u) << "the other grant still starts it";
}

TEST_F(AbilityRuntime, ASourceChangeEndsInputLost)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    Grant(actor, charge);
    Step({ Press(kFire) });
    Entities.TryGet<InputActionSourceRef>(actor)->Source = 7;
    Step();
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).front().EndReason, AbilityEndReason::InputLost);
}

TEST_F(AbilityRuntime, ATogglePressEndsWhatItStarted)
{
    const AbilityId toggle = Define("asset://abilities/toggle.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire", "policy": "toggle" },
        "phases": [ { "key": "p_on", "hold": [ { "key": "o_on", "lease": "test.hold" } ],
            "then": [ { "key": "t_off", "on": "release", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, toggle);
    Step({ Press(kFire), Release(kFire) });
    Steps(5);
    EXPECT_NE(LiveRecord(actor, toggle), nullptr) << "a key coming up is not a toggle";
    Step({ Press(kFire) });
    EXPECT_EQ(LiveRecord(actor, toggle), nullptr);
    EXPECT_EQ(Held.Count(LeaseStep::Release), 1u);
    EXPECT_TRUE(Traced(AbilityTraceEvent::Started).size() == 1u) << "the second press only ends it";
}

TEST_F(AbilityRuntime, RequestsActivateAndCancelByName)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    Grant(actor, charge);
    AbilityRequestQueue& queue = Entities.GetResource<AbilityRequestQueue>();
    ASSERT_TRUE(queue.Push({ .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = charge }));
    Step();
    const AbilityRecord* record = LiveRecord(actor, charge);
    ASSERT_NE(record, nullptr);
    EXPECT_GE(record->Key.Origin, kAbilityRequestOrigin);
    ASSERT_TRUE(queue.Push({ .Kind = AbilityRequestKind::Cancel, .Actor = actor, .Ability = charge }));
    Step();
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).front().EndReason, AbilityEndReason::ExplicitCancel);
}

TEST_F(AbilityRuntime, AReloadMidChargeEndsItAndLetsGoOfItsHolds)
{
    const AbilityId charge = DefineCharge();
    const EntityId actor = Actor();
    Entities.TryGet<GameplayTagContainer>(actor)->Grant(Tag("Weapon.CTX.Mode.Charge"));
    Grant(actor, charge);
    Step({ Press(kFire) });
    (void)DefineCharge();
    Step();
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).front().EndReason, AbilityEndReason::HotReload);
    EXPECT_EQ(Posed.Count(LeaseStep::Release), 1u);
}

TEST_F(AbilityRuntime, ADestroyedActorLeavesNoHeldEffectsOrTagsBehind)
{
    const AbilityId drain = Define("asset://abilities/drain.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
        "phases": [ { "key": "p_on",
            "hold": [ { "key": "o_cool", "lease": "effect.hold",
                        "arguments": { "target": { "port": "actor" },
                                       "effect": { "data": "asset://effects/charge_cooldown.sdata" } } } ],
            "then": [ { "key": "t_off", "on": "release", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, drain);
    Step({ Press(kFire) });
    std::size_t effects = 0;
    Query<Read<ActiveEffect>> query(Entities);
    query.ForEachChunk([&](auto& view) { effects += view.Count(); });
    ASSERT_EQ(effects, 1u);
    Entities.DestroyEntity(actor);
    Step();
    effects = 0;
    query.ForEachChunk([&](auto& view) { effects += view.Count(); });
    EXPECT_EQ(effects, 0u) << "the effect's owner is gone, so the effect is";
}

TEST_F(AbilityRuntime, AWarmPassAllocatesNothing)
{
    const AbilityId automatic = Define("asset://abilities/automatic.sdata", kAutomatic);
    const EntityId actor = Actor();
    Entities.TryGet<AttributeSet>(actor)->SetBase(Ammo, 30.0f);
    Grant(actor, automatic);
    Step({ Press(kFire) });
    Steps(8);
    Marks.Values.reserve(64);

    // The ability pass alone: the effect passes the fixture also runs are not its to answer for.
    std::size_t allocations = 0;
    for (int i = 0; i < 8; ++i)
    {
        InputActionState& state = Entities.GetResource<InputActionState>();
        std::span<InputActionValue> values = state.BeginTick(Now);
        values[kFire].Flags = InputActionFlags::Held;
        AdvanceActorTime(Entities);
        const std::size_t before = AllocationCount();
        RunAbilityPass(Entities, Host, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now }, nullptr, Settings);
        allocations += AllocationCount() - before;
        ++Now;
    }
    EXPECT_EQ(allocations, 0u) << "pulses, commits and invokes of a running ability";
    EXPECT_EQ(Marks.Values.size(), 5u) << "pulses on ticks 12 and 16 were measured";
}


TEST_F(AbilityRuntime, TheAbilityVerbsGrantRevokeActivateAndCancel)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    const VerbRegistry& catalog = Verbs->Registry();
    Tokens.push_back(Verbs->Bind<&InvokeAbilityGrant>(catalog.Find(kAbilityGrantVerb), AbilityVerbs));
    Tokens.push_back(Verbs->Bind<&InvokeAbilityRevoke>(catalog.Find(kAbilityRevokeVerb), AbilityVerbs));

    const auto invoke = [&](std::string_view verb, bool keyed) {
        VerbBindingDesc desc;
        desc.Key = std::string(verb) + ".test";
        desc.KeyId = MakeVerbBindingKey(desc.Key);
        desc.VerbName = std::string(verb);
        desc.Inputs = { "target" };
        VerbBindingArgument target;
        target.Key = "target";
        target.Source = VerbArgumentSource::Input;
        target.Text = "target";
        VerbBindingArgument ability;
        ability.Key = "ability";
        ability.Source = VerbArgumentSource::DataAsset;
        ability.Text = "asset://abilities/charge.sdata";
        desc.Arguments = { target, ability };
        if (keyed)
        {
            VerbBindingArgument key;
            key.Key = "key";
            key.Literal = JsonValue("scripted");
            desc.Arguments.push_back(key);
        }
        CompiledVerbBinding binding;
        std::vector<std::string> errors;
        EXPECT_TRUE(CompileVerbBinding(desc, MakeVerbBindingEnvironment(Entities), binding, errors))
            << (errors.empty() ? "" : errors.front());
        const AuthoredValue input = AuthoredValue::Entity(actor);
        return Verbs->Invoke(binding, std::span(&input, 1)).Status;
    };

    EXPECT_EQ(invoke(kAbilityActivateVerb, false), VerbAdmission::Accepted);
    Step();
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused).front().Refusal, AbilityRefusal::NotGranted);

    EXPECT_EQ(invoke(kAbilityGrantVerb, true), VerbAdmission::Accepted);
    EXPECT_EQ(invoke(kAbilityActivateVerb, false), VerbAdmission::Accepted);
    Step();
    EXPECT_NE(LiveRecord(actor, charge), nullptr);
    EXPECT_EQ(invoke(kAbilityCancelVerb, false), VerbAdmission::Accepted);
    Step();
    EXPECT_EQ(LiveRecord(actor, charge), nullptr);

    EXPECT_EQ(invoke(kAbilityRevokeVerb, true), VerbAdmission::Accepted);
    EXPECT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, 0u);
    EXPECT_EQ(invoke(kAbilityRevokeVerb, true), VerbAdmission::Refused);
}

TEST_F(AbilityRuntime, AnEffectGrantsItsAbilitiesForAsLongAsItLasts)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    EffectDefinition weapon{};
    weapon.Duration = EffectDuration::Infinite;
    const EffectId equipped = Entities.GetResource<EffectRegistry>().Define("asset://effects/weapon.sdata", weapon);
    DeclareEffectGrants(Entities, equipped,
                        { AbilityGrantDeclaration{ .Key = "w_charge", .Ability = "asset://abilities/charge.sdata",
                                                   .Override = { .Action = "Input.AltFire" } } });
    const EntityId actor = Actor();

    const EntityId held = ApplyEffect(Entities, actor, equipped, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now });
    Step({ Press(kAlt) });
    EXPECT_NE(LiveRecord(actor, charge), nullptr) << "granted in the pass after it applied, bound as the effect says";
    const AbilityGrants& grants = *Entities.TryGet<AbilityGrants>(actor);
    ASSERT_EQ(grants.Count, 1u);
    EXPECT_EQ(grants.Entries[0].Kind, AbilityGrantKind::Effect);
    EXPECT_EQ(grants.Entries[0].Effect, held);

    Step();
    EXPECT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, 1u) << "granted once, not once per pass";
    ASSERT_TRUE(RemoveEffect(Entities, held));
    Step();
    EXPECT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, 0u);
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).front().EndReason, AbilityEndReason::GrantRevoked);
}
