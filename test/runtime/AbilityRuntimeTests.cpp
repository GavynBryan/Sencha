// The ability pass: admission, phases, input edges, guards, commits, cooldowns,
// leases and every way an activation ends, driven through real input records and
// the real dispatchers.

#include <abilities/AbilityCueLog.h>
#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityRequestQueue.h>
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
#include <time/ActorTimeLeases.h>
#include <time/ActorTimeSystem.h>
#include <time/SimClock.h>
#include <world/transform/TransformComponents.h>

#include <AllocationCounter.h>
#include <CounterCostProvider.h>

#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
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

constexpr std::string_view kGrantedInstant = R"({
    "phases": [ { "key": "p_go", "then": [ { "key": "t_end", "to": "end" } ] } ] })";

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
        EXPECT_TRUE(InstallEngineVocabulary(Entities));

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
        QueryTokens.push_back(Queries->Bind<&EvaluatePhysicsOverlap>(Queries->Registry().Find(kPhysicsOverlapQuery),
                                                                     std::as_const(Traces)));
        Host = AbilityHost{ .Verbs = Verbs.get(), .Queries = Queries.get() };
    }

    void DeclareCounters()
    {
        EXPECT_TRUE(DeclareCounterCostProvider(Entities));
        EffectDefinition stamina{};
        stamina.Modifiers = { { Energy, ModifierOp::Add, -10.0f } };
        Entities.GetResource<EffectRegistry>().Define("asset://effects/energy_10.sdata", stamina);
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

    std::vector<EffectId> DefineGrantingEffects(std::size_t count)
    {
        EffectDefinition lasting{};
        lasting.Duration = EffectDuration::Infinite;
        std::vector<EffectId> effects;
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::string ability = "asset://abilities/granted_" + std::to_string(i) + ".sdata";
            (void)Define(ability, kGrantedInstant);
            const EffectId effect = Entities.GetResource<EffectRegistry>().Define(
                "asset://effects/granting_" + std::to_string(i) + ".sdata", lasting);
            DeclareEffectGrants(Entities, effect, { AbilityGrantDeclaration{ .Key = "g", .Ability = ability, .Override = {} } });
            effects.push_back(effect);
        }
        return effects;
    }

    EntityId ApplyNow(EntityId target, EffectId effect)
    {
        return ApplyEffect(Entities, target, effect, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now });
    }

    [[nodiscard]] bool HoldsGrantOf(EntityId actor, EntityId effect)
    {
        const AbilityGrants& grants = *Entities.TryGet<AbilityGrants>(actor);
        return std::any_of(grants.Entries, grants.Entries + grants.Count,
                           [&](const AbilityGrant& grant) { return grant.Holder == effect; });
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
        AdvanceActorTime(Entities, Now);
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
        const ActiveAbilities& active = *Entities.TryGet<ActiveAbilities>(actor);
        AbilityActivations& activations = Entities.GetResource<AbilityActivations>();
        for (std::size_t slot = 0; slot < kAbilitySlotCapacity; ++slot)
            if (const AbilityRecord& record = activations.Record(active, slot);
                record.State == AbilityRecordState::Live && record.Key.Ability == ability)
                return &record;
        return nullptr;
    }

    [[nodiscard]] const AbilityRecordLocals* LiveLocals(EntityId actor, AbilityId ability)
    {
        const ActiveAbilities& active = *Entities.TryGet<ActiveAbilities>(actor);
        AbilityActivations& activations = Entities.GetResource<AbilityActivations>();
        for (std::size_t slot = 0; slot < kAbilitySlotCapacity; ++slot)
            if (const AbilityRecord& record = activations.Record(active, slot);
                record.State == AbilityRecordState::Live && record.Key.Ability == ability)
                return &activations.Locals(active, slot);
        return nullptr;
    }

    [[nodiscard]] std::size_t LiveCount(EntityId actor)
    {
        const auto records = AbilityRecordsOf(Entities, actor);
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

TEST_F(AbilityRuntime, AGrantBoundToAnUnknownActionIsReported)
{
    std::vector<std::string> lines;
    struct Captured : ILogSink
    {
        std::vector<std::string>* Lines = nullptr;
        void Write(LogLevel, std::string_view, std::string_view message) override { Lines->emplace_back(message); }
    };
    LoggingProvider logging;
    logging.AddSink<Captured>().Lines = &lines;
    Host.Logging = &logging;
    const AbilityId primary = Define("asset://abilities/primary.sdata", kInstant);
    Grant(Actor(), primary, AbilityBindingDecl{ .Action = "Input.PrimaryFrie" });
    Steps(3);
    ASSERT_EQ(std::ranges::count_if(lines, [](const std::string& line) {
                  return line.find("Input.PrimaryFrie") != std::string::npos;
              }),
              1)
        << "named once, when the bindings were resolved, not every tick";
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

constexpr std::string_view kBowShot = R"({
    "input_default": { "action": "Input.PrimaryFire" },
    "costs": [ { "key": "g_shot",
                 "effects": [ { "data": "asset://effects/energy_10.sdata" } ],
                 "resources": [ { "key": "r_arrow", "provider": "test.counter",
                                  "arguments": { "holder": { "port": "actor" } } } ] } ],
    "phases": [ { "key": "p_draw",
        "enter": [ { "key": "o_pay", "op": "commit", "groups": ["g_shot"] },
                   { "key": "o_mark", "op": "invoke", "verb": "test.mark", "arguments": { "value": { "const": 1 } } } ],
        "then": [ { "key": "t_end", "to": "end" } ] } ] })";

TEST_F(AbilityRuntime, AnArrowAndStaminaArePaidTogetherOrNotAtAll)
{
    DeclareCounters();
    const AbilityId shot = Define("asset://abilities/bow_shot.sdata", kBowShot);
    const EntityId actor = Actor();
    Grant(actor, shot);
    Counters& quiver = Entities.GetResource<Counters>();

    quiver.Held[actor] = 0;
    Step({ Press(kFire), Release(kFire) });
    EXPECT_TRUE(Marks.Values.empty());
    EXPECT_FLOAT_EQ(Base(actor, Energy), 10.0f) << "no arrow: the stamina is untouched";

    quiver.Held[actor] = 2;
    Entities.TryGet<AttributeSet>(actor)->SetBase(Energy, 5.0f);
    Step({ Press(kFire), Release(kFire) });
    EXPECT_TRUE(Marks.Values.empty());
    EXPECT_EQ(quiver.Held[actor], 2) << "no stamina: the arrows are untouched";

    Entities.TryGet<AttributeSet>(actor)->SetBase(Energy, 10.0f);
    Step({ Press(kFire), Release(kFire) });
    EXPECT_EQ(Marks.Values, std::vector<double>{ 1.0 });
    EXPECT_EQ(quiver.Held[actor], 1);
    EXPECT_FLOAT_EQ(Base(actor, Energy), 0.0f);
}

// Admitted on the ability's default, the start would be left unpaid.
TEST_F(AbilityRuntime, AStartCostReadsTheGrantsParameterAtAdmission)
{
    DeclareCounters();
    const AbilityId sip = Define("asset://abilities/sip.sdata", R"({
        "parameters": [ { "key": "doses", "default": 1 } ],
        "costs": [ { "key": "g_start", "when": "start",
                     "resources": [ { "key": "r_dose", "provider": "test.counter",
                                      "arguments": { "holder": { "port": "actor" }, "count": { "param": "doses" } } } ] } ],
        "phases": [ { "key": "p_sip",
            "enter": [ { "key": "o_mark", "op": "invoke", "verb": "test.mark", "arguments": { "value": { "const": 1 } } } ],
            "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    (void)GrantAbility(*Entities.TryGet<AbilityGrants>(actor), sip, AbilityGrantKind::Authored,
                       Entities.GetResource<AbilityGrantEntries>().Intern(
                           { .Key = "g", .Ability = sip, .Override = {}, .Parameters = { { "doses", 3.0 } } }));
    Entities.GetResource<Counters>().Held[actor] = 2;
    Entities.GetResource<AbilityRequestQueue>().Push({ .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = sip });
    Step();
    EXPECT_TRUE(Marks.Values.empty()) << "it ran without paying its three doses";
    EXPECT_EQ(Entities.GetResource<Counters>().Held[actor], 2);
    ASSERT_FALSE(Traced(AbilityTraceEvent::Refused).empty());
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused).back().Refusal, AbilityRefusal::CostFailed);
}

namespace
{
bool TranslateQuiver(const World&, const AbilityCostRow& row, std::vector<AuthoredValue>& arguments)
{
    EntityId bearer;
    if (row.Arguments.empty() || !row.Arguments[0].TryGetEntity(bearer) || !bearer.IsValid())
        return false;
    arguments.push_back(AuthoredValue::Entity(bearer));
    arguments.push_back(AuthoredValue::Int(1));
    return true;
}

AbilityCostProviderDefinition Quiver(std::string name, std::string spendsThrough)
{
    AbilityCostProviderDefinition quiver;
    quiver.Name = std::move(name);
    quiver.Arguments.Children = { MakeDataField(DataFieldKind::Entity, "bearer", "Bearer") };
    quiver.SpendsThrough = std::move(spendsThrough);
    quiver.Translate = &TranslateQuiver;
    return quiver;
}
}

TEST_F(AbilityRuntime, ARowSpentThroughAnotherProviderJoinsItsBatch)
{
    DeclareCounters();
    {
        AbilityCostProviderRegistrationScope scope(Entities.GetResource<AbilityCostProviderCatalog>(), "test.quiver");
        ASSERT_TRUE(scope.Declare(Quiver("test.quiver", "test.counter")));
        ASSERT_TRUE(scope.Commit());
    }
    const AbilityId volley = Define("asset://abilities/volley.sdata", R"({
        "costs": [ { "key": "g_volley",
                     "resources": [ { "key": "r_nock", "provider": "test.counter",
                                      "arguments": { "holder": { "port": "actor" } } },
                                    { "key": "r_draw", "provider": "test.quiver",
                                      "arguments": { "bearer": { "port": "actor" } } } ] } ],
        "phases": [ { "key": "p_loose",
            "enter": [ { "key": "o_pay", "op": "commit", "groups": ["g_volley"] },
                       { "key": "o_mark", "op": "invoke", "verb": "test.mark", "arguments": { "value": { "const": 1 } } } ],
            "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, volley);
    Counters& counters = Entities.GetResource<Counters>();
    counters.Held[actor] = 1;
    Entities.GetResource<AbilityRequestQueue>().Push({ .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = volley });
    Step();
    EXPECT_TRUE(Marks.Values.empty()) << "one arrow cannot pay both rows";
    EXPECT_EQ(counters.Held[actor], 1);

    counters.Held[actor] = 2;
    const std::size_t checks = counters.Checks;
    Entities.GetResource<AbilityRequestQueue>().Push({ .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = volley });
    Step();
    EXPECT_EQ(Marks.Values, std::vector<double>{ 1.0 });
    EXPECT_EQ(counters.Held[actor], 0);
    EXPECT_EQ(counters.Checks, checks + 1) << "one batch for the one resource";
}

TEST_F(AbilityRuntime, AProviderOwnsItsResourceOrSpendsThroughAnotherNeverBoth)
{
    DeclareCounters();
    AbilityCostProviderDefinition both = Quiver("test.both", "test.counter");
    both.Check = &CheckCounters;
    both.Apply = &ApplyCounters;
    AbilityCostProviderDefinition neither;
    neither.Name = "test.neither";
    AbilityCostProviderRegistrationScope scope(Entities.GetResource<AbilityCostProviderCatalog>(), "test.bad");
    EXPECT_FALSE(scope.Declare(std::move(both)));
    EXPECT_FALSE(scope.Declare(std::move(neither)));
    EXPECT_FALSE(scope.Declare(Quiver("test.self", "test.self")));
}

TEST_F(AbilityRuntime, ASpendThroughATranslatingProviderFailsItsGroup)
{
    DeclareCounters();
    {
        AbilityCostProviderRegistrationScope scope(Entities.GetResource<AbilityCostProviderCatalog>(), "test.quiver");
        ASSERT_TRUE(scope.Declare(Quiver("test.quiver", "test.counter")));
        ASSERT_TRUE(scope.Declare(Quiver("test.sling", "test.quiver")));
        ASSERT_TRUE(scope.Commit());
    }
    const AbilityId sling = Define("asset://abilities/sling.sdata", R"({
        "costs": [ { "key": "g_sling", "when": "start",
                     "resources": [ { "key": "r_stone", "provider": "test.sling",
                                      "arguments": { "bearer": { "port": "actor" } } } ] } ],
        "phases": [ { "key": "p_throw", "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, sling);
    Entities.GetResource<Counters>().Held[actor] = 5;
    Entities.GetResource<AbilityRequestQueue>().Push({ .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = sling });
    Step();
    EXPECT_EQ(LiveRecord(actor, sling), nullptr);
    EXPECT_EQ(Entities.GetResource<Counters>().Held[actor], 5);
    ASSERT_FALSE(Traced(AbilityTraceEvent::Refused).empty());
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused).back().Refusal, AbilityRefusal::CostFailed);
}

TEST_F(AbilityRuntime, AProviderChecksEveryGroupsRowsInOneBatch)
{
    DeclareCounters();
    const AbilityId volley = Define("asset://abilities/volley.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "costs": [ { "key": "g_left", "resources": [ { "key": "r_a", "provider": "test.counter",
                                                       "arguments": { "holder": { "port": "actor" } } } ] },
                   { "key": "g_right", "resources": [ { "key": "r_b", "provider": "test.counter",
                                                        "arguments": { "holder": { "port": "actor" }, "count": 1 } } ] } ],
        "phases": [ { "key": "p_loose",
            "enter": [ { "key": "o_pay", "op": "commit", "groups": ["g_left", "g_right"] } ],
            "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, volley);
    Counters& quiver = Entities.GetResource<Counters>();
    quiver.Held[actor] = 1;
    Step({ Press(kFire), Release(kFire) });
    EXPECT_EQ(quiver.Checks, 1u) << "once, with both groups' rows";
    EXPECT_EQ(quiver.Held[actor], 1) << "one arrow does not pay for two";
    ASSERT_FALSE(Traced(AbilityTraceEvent::Ended).empty());
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).back().EndReason, AbilityEndReason::CommitFailed);
}

TEST_F(AbilityRuntime, ASourceTargetedEffectCostChargesTheSource)
{
    const AbilityId fire = Define("asset://abilities/clip_fire.sdata", R"({
        "costs": [ { "key": "g_round", "target": "source", "effects": [ { "data": "asset://effects/ammo_1.sdata" } ] } ],
        "phases": [ { "key": "p_fire", "enter": [ { "key": "o_pay", "op": "commit", "groups": ["g_round"] } ],
            "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    const EntityId pistol = Entities.CreateEntity();
    Entities.AddComponent<AttributeSet>(pistol);
    Entities.TryGet<AttributeSet>(pistol)->Add(Ammo, 18.0f);
    (void)GrantAbility(*Entities.TryGet<AbilityGrants>(actor), fire, AbilityGrantKind::Request,
                       Entities.GetResource<AbilityGrantEntries>().Intern({ "g", fire, {} }), {}, pistol);
    Entities.GetResource<AbilityRequestQueue>().Push(
        { .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = fire });
    Step();
    EXPECT_FLOAT_EQ(Base(pistol, Ammo), 17.0f) << "the clip lives on the weapon";
    EXPECT_FLOAT_EQ(Base(actor, Ammo), 3.0f);
}

TEST_F(AbilityRuntime, AnUnknownProviderOrArgumentIsRefusedAtDeclaration)
{
    DeclareCounters();
    const auto refusal = [&](std::string_view json) {
        const std::optional<JsonValue> data = JsonParse(json);
        AbilityDeclaration declaration;
        std::vector<AssetRef> dependencies;
        std::vector<AbilityDiagnostic> diagnostics;
        EXPECT_TRUE(CompileAbilityDeclaration(*data, declaration, dependencies, diagnostics)) << Describe(diagnostics);
        AbilityDefinition definition;
        EXPECT_FALSE(ResolveAbilityDeclaration(declaration, "asset://abilities/x.sdata",
                                               MakeAbilityResolveEnvironment(Entities), definition, diagnostics));
        return diagnostics.empty() ? std::string{} : diagnostics.front().Code;
    };
    EXPECT_EQ(refusal(R"({ "costs": [ { "key": "g", "resources": [ { "key": "r", "provider": "test.nothing" } ] } ],
        "phases": [ { "key": "p", "enter": [ { "key": "o", "op": "commit", "groups": ["g"] } ],
                      "then": [ { "key": "t", "to": "end" } ] } ] })"),
              "provider_unknown");
    EXPECT_EQ(refusal(R"({ "costs": [ { "key": "g", "resources": [ { "key": "r", "provider": "test.counter",
                                         "arguments": { "holder": { "port": "actor" }, "colour": 3 } } ] } ],
        "phases": [ { "key": "p", "enter": [ { "key": "o", "op": "commit", "groups": ["g"] } ],
                      "then": [ { "key": "t", "to": "end" } ] } ] })"),
              "schema");
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
        const auto records = AbilityRecordsOf(Entities, actor);
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

TEST_F(AbilityRuntime, AHoldForTheActivationOutlivesItsPhase)
{
    const AbilityId kept = Define("asset://abilities/kept.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
        "phases": [
          { "key": "p_first",
            "hold": [ { "key": "o_kept", "lease": "test.hold", "scope": "activation" },
                      { "key": "o_phase", "lease": "test.hold" } ],
            "then": [ { "key": "t_next", "on": "timeout", "ticks": 1, "to": "p_second" } ] },
          { "key": "p_second", "then": [ { "key": "t_end", "on": "release", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, kept);

    Step({ Press(kFire) });
    ASSERT_EQ(Held.Calls.size(), 2u);
    const auto keptKey = Held.Calls[0].second;
    const auto phaseKey = Held.Calls[1].second;
    Step({});
    Step({});
    ASSERT_EQ(Held.Calls.size(), 3u) << "leaving the phase lets go of its own hold alone";
    EXPECT_EQ(Held.Calls[2].first, LeaseStep::Release);
    EXPECT_EQ(Held.Calls[2].second, phaseKey);
    Step({ Release(kFire) });
    ASSERT_EQ(Held.Calls.size(), 4u) << "the end of the activation lets go of the rest";
    EXPECT_EQ(Held.Calls[3].first, LeaseStep::Release);
    EXPECT_EQ(Held.Calls[3].second, keptKey);
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

TEST_F(AbilityRuntime, AContextNamingAnUnknownTagStartsNothingUntilTheTagIsHeld)
{
    const AbilityId primary = Define("asset://abilities/primary.sdata", kInstant);
    const EntityId actor = Actor();
    AbilityBindingDecl grounded;
    grounded.Context = AbilityTagQueryDecl{ .All = { "State.Grounded" }, .Any = {}, .None = {} };
    Grant(actor, primary, grounded);

    Step({ Press(kFire) });
    EXPECT_TRUE(Marks.Values.empty()) << "a context that cannot be read refuses, never admits everything";

    ASSERT_TRUE(Entities.GetResource<GameplayTagRegistry>().RegisterTag("State.Grounded").has_value());
    Step({ Release(kFire), Press(kFire) });
    EXPECT_TRUE(Marks.Values.empty()) << "once the tag exists the context reads it, and the actor lacks it";

    Entities.TryGet<GameplayTagContainer>(actor)->Grant(Tag("State.Grounded"));
    Step({ Release(kFire), Press(kFire) });
    EXPECT_EQ(Marks.Values.size(), 1u);
}

TEST_F(AbilityRuntime, TwoGrantsOfOneAbilityStartTwoActivationsNamedApart)
{
    const AbilityId watch = Define("asset://abilities/watch.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire", "consume": "observe" },
        "instances": 2,
        "phases": [ { "key": "p_wait", "then": [ { "key": "t_end", "on": "timeout", "ticks": 30, "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, watch, {}, "g_left");
    Grant(actor, watch, {}, "g_right");
    Step({ Press(kFire) });
    std::vector<std::uint64_t> identities;
    for (const AbilityRecord& record : AbilityRecordsOf(Entities, actor))
        if (record.State == AbilityRecordState::Live)
            identities.push_back(record.Identity);
    ASSERT_EQ(identities.size(), 2u) << "one press, observed by both grants";
    EXPECT_NE(identities[0], identities[1]) << "leases, cues and guesses key on the identity";
}

TEST_F(AbilityRuntime, AnOpsRepeatedCueIsNumberedByWhatItEmittedNotWhatTheRingKept)
{
    // A loop the compiler refuses (zero_time_cycle), closed after compiling: A and B
    // alternate within the tick until the step budget ends them, so A's shot cue
    // repeats after B's cues have filled the ring.
    std::string cues;
    for (std::size_t i = 0; i < kAbilityCueLogCapacity; ++i)
        cues += std::format(R"({{ "key": "o_b{}", "op": "cue", "cue": "cue.impact" }}{})", i,
                            i + 1 < kAbilityCueLogCapacity ? ", " : "");
    const std::optional<JsonValue> data = JsonParse(std::format(R"({{
        "input_default": {{ "action": "Input.PrimaryFire", "policy": "press_release" }},
        "phases": [
          {{ "key": "p_a", "enter": [ {{ "key": "o_a", "op": "cue", "cue": "cue.shot" }} ],
            "then": [ {{ "key": "t_b", "on": "release", "to": "p_b" }} ] }},
          {{ "key": "p_b", "enter": [ {} ], "then": [ {{ "key": "t_a", "on": "timeout", "ticks": 1, "to": "p_a" }} ] }} ] }})",
                                                                cues));
    AbilityDeclaration declaration;
    std::vector<AssetRef> dependencies;
    std::vector<AbilityDiagnostic> diagnostics;
    ASSERT_TRUE(CompileAbilityDeclaration(*data, declaration, dependencies, diagnostics)) << Describe(diagnostics);
    declaration.Phases[1].Then[0].Trigger = AbilityTransitionTrigger::Immediate;
    AbilityDefinition definition;
    ASSERT_TRUE(ResolveAbilityDeclaration(declaration, "asset://abilities/looping.sdata",
                                          MakeAbilityResolveEnvironment(Entities), definition, diagnostics))
        << Describe(diagnostics);
    const AbilityId looping =
        Entities.GetResource<AbilityRegistry>().Define("asset://abilities/looping.sdata", std::move(definition));
    const EntityId actor = Actor();
    Grant(actor, looping);
    Step({ Press(kFire), Release(kFire) });
    ASSERT_EQ(Traced(AbilityTraceEvent::Ended).front().EndReason, AbilityEndReason::StepBudget);

    const AbilityCueLog& log = *Entities.TryGet<AbilityCueLog>(actor);
    ASSERT_EQ(std::ranges::count(log.Entries, Tag("cue.shot"), &AbilityCueEntry::Cue), 1)
        << "only the last shot cue is still in the ring";
    const auto shot = std::ranges::find(log.Entries, Tag("cue.shot"), &AbilityCueEntry::Cue);
    EXPECT_EQ(shot->Ordinal, Settings.StepBudget / 2) << "the op's fifth shot cue this tick";
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
    queue.Push({ .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = charge });
    Step();
    const AbilityRecord* record = LiveRecord(actor, charge);
    ASSERT_NE(record, nullptr);
    EXPECT_GE(record->Key.Origin, kAbilityRequestOrigin);
    queue.Push({ .Kind = AbilityRequestKind::Cancel, .Actor = actor, .Ability = charge });
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
        AdvanceActorTime(Entities, Now);
        const std::size_t before = AllocationCount();
        RunAbilityPass(Entities, Host, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now }, nullptr, Settings);
        allocations += AllocationCount() - before;
        ++Now;
    }
    EXPECT_EQ(allocations, 0u) << "pulses, commits and invokes of a running ability";
    EXPECT_EQ(Marks.Values.size(), 5u) << "pulses on ticks 12 and 16 were measured";
}


TEST_F(AbilityRuntime, AStartThatGrantsAnotherAbilityLeavesThePressesOtherBindingsIntact)
{
    Tokens.push_back(Verbs->Bind<&InvokeAbilityGrant>(Verbs->Registry().Find(kAbilityGrantVerb), AbilityVerbs));
    (void)Define("asset://abilities/primary.sdata", kInstant);
    const AbilityId granter = Define("asset://abilities/granter.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "phases": [ { "key": "p_grant",
            "enter": [ { "key": "o_grant", "op": "invoke", "verb": "ability.grant",
                         "arguments": { "target": { "port": "actor" },
                                        "ability": { "data": "asset://abilities/primary.sdata" },
                                        "key": { "const": "g_granted" } } } ],
            "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const AbilityId watcher = Define("asset://abilities/watch.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire", "consume": "observe" },
        "phases": [ { "key": "p_fire", "enter": [ { "key": "o_mark", "op": "invoke", "verb": "test.mark",
            "arguments": { "value": { "const": 3 } } } ], "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, granter, {}, "g_granter");
    Grant(actor, watcher, {}, "g_watch");
    const std::size_t interned = Entities.GetResource<AbilityGrantEntries>().Size();

    // The grant interns an entry mid-press, which may move every binding the press resolved.
    Step({ Press(kFire) });
    EXPECT_EQ(Entities.GetResource<AbilityGrantEntries>().Size(), interned + 1);
    EXPECT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, 3u);
    EXPECT_EQ(Marks.Values, (std::vector<double>{ 3.0 })) << "the observer still starts from the same press";
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

// Two rings, each an entity whose Infinite effect grants Fireball with its own power and priority.
constexpr std::string_view kFireball = R"({
    "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
    "parameters": [ { "key": "power", "default": 0 } ],
    "phases": [ { "key": "p_cast",
        "enter": [ { "key": "o_mark", "op": "invoke", "verb": "test.mark", "arguments": { "value": { "param": "power" } } } ],
        "then": [ { "key": "t_loose", "on": "release", "to": "end" } ] } ] })";

class TwoRings : public AbilityRuntime
{
protected:
    void Wear(std::int32_t firePriority, std::int32_t emberPriority)
    {
        Fireball = Define("asset://abilities/fireball.sdata", kFireball);
        Hero = Actor();
        Fire = Ring("asset://effects/ring_fire.sdata", firePriority, 10.0);
        Ember = Ring("asset://effects/ring_ember.sdata", emberPriority, 14.0);
        Step();
    }

    EntityId Ring(std::string_view path, std::int32_t priority, double power)
    {
        EffectDefinition worn{};
        worn.Duration = EffectDuration::Infinite;
        const EffectId effect = Entities.GetResource<EffectRegistry>().Define(path, worn);
        DeclareEffectGrants(Entities, effect,
                            { AbilityGrantDeclaration{ .Key = "g_fireball", .Ability = "asset://abilities/fireball.sdata",
                                                       .Override = { .Priority = priority },
                                                       .Parameters = { { "power", power } } } });
        const EntityId ring = Entities.CreateEntity();
        (void)ApplyEffect(Entities, Hero, effect, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now },
                          LeaseOwner{ .Entity = ring });
        return ring;
    }

    AbilityId Fireball;
    EntityId Hero;
    EntityId Fire;
    EntityId Ember;
};

TEST_F(TwoRings, APressCastsFromTheHigherSlotsRingAndFallsBackWhenItIsTakenOff)
{
    Wear(20, 21);
    ASSERT_EQ(Entities.TryGet<AbilityGrants>(Hero)->Count, 2u) << "one entry each: their powers differ";

    Step({ Press(kFire) });
    EXPECT_EQ(Marks.Values, std::vector<double>{ 14.0 }) << "the ember ring's power";
    EXPECT_TRUE(HasLiveActivationFrom(Entities, Hero, Ember));

    ASSERT_EQ(RemoveEffectsOwnedBy(Entities, LeaseOwner{ .Entity = Ember }), 1u);
    Steps(2);
    EXPECT_FALSE(HasLiveActivationFrom(Entities, Hero, Ember));
    ASSERT_FALSE(Traced(AbilityTraceEvent::Ended).empty());
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).back().EndReason, AbilityEndReason::GrantRevoked);

    Step({ Release(kFire) });
    Step({ Press(kFire) });
    EXPECT_EQ(Marks.Values, (std::vector<double>{ 14.0, 10.0 })) << "the fire ring's power";
    EXPECT_TRUE(HasLiveActivationFrom(Entities, Hero, Fire));
}

TEST_F(TwoRings, RingsTiedOnPriorityCastNothingAndSaySo)
{
    Wear(20, 20);
    Step({ Press(kFire) });
    EXPECT_TRUE(Marks.Values.empty()) << "nothing collapses silently";
    EXPECT_FALSE(Traced(AbilityTraceEvent::BindingAmbiguous).empty());
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
    EXPECT_EQ(grants.Entries[0].Holder, held);

    Step();
    EXPECT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, 1u) << "granted once, not once per pass";
    ASSERT_TRUE(RemoveEffect(Entities, held));
    Step();
    EXPECT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, 0u);
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).front().EndReason, AbilityEndReason::GrantRevoked);
}

constexpr std::string_view kReadSource = R"({
    "input_default": { "action": "Input.AltFire", "policy": "press_release" },
    "locals": [ { "key": "l_source", "type": "entity" } ],
    "phases": [ { "key": "p_hold",
        "enter": [ { "key": "o_read", "op": "set", "local": "l_source", "from": { "port": "source" } } ],
        "then": [ { "key": "t_stop", "on": "release", "to": "end" } ] } ] })";

TEST_F(AbilityRuntime, ARequestTakesTheHighestPriorityGrantThenTheLowestId)
{
    const AbilityId read = Define("asset://abilities/read_source.sdata", kReadSource);
    const EntityId actor = Actor();
    const EntityId low = Entities.CreateEntity();
    const EntityId high = Entities.CreateEntity();
    const EntityId later = Entities.CreateEntity();
    AbilityGrantEntries& entries = Entities.GetResource<AbilityGrantEntries>();
    const auto grant = [&](std::int32_t priority, EntityId source) {
        const std::uint16_t entry = entries.Intern({ "g_" + std::to_string(priority), read, { .Priority = priority } });
        return GrantAbility(*Entities.TryGet<AbilityGrants>(actor), read, AbilityGrantKind::Request, entry, {}, source);
    };
    (void)grant(20, low);
    const AbilityGrantId top = grant(21, high);
    (void)grant(21, later);
    const auto request = [&](EntityId source) {
        Entities.GetResource<AbilityRequestQueue>().Push(
            { .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = read, .Source = source });
        Step();
    };

    request({});
    ASSERT_NE(LiveRecord(actor, read), nullptr);
    EXPECT_EQ(LiveRecord(actor, read)->Grant, top) << "the highest priority, and of two at it the first granted";
    Entities.GetResource<AbilityRequestQueue>().Push({ .Kind = AbilityRequestKind::Cancel, .Actor = actor, .Ability = read });
    Step();

    request(low);
    ASSERT_NE(LiveRecord(actor, read), nullptr);
    EXPECT_TRUE(HasLiveActivationFrom(Entities, actor, low)) << "a source names its grant exactly";
}

TEST_F(AbilityRuntime, ARequestFromASourceWithNoSuchGrantIsNotGranted)
{
    const AbilityId read = Define("asset://abilities/read_source.sdata", kReadSource);
    const EntityId actor = Actor();
    Grant(actor, read);
    Entities.GetResource<AbilityRequestQueue>().Push(
        { .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = read, .Source = Entities.CreateEntity() });
    Step();
    EXPECT_EQ(LiveRecord(actor, read), nullptr);
    ASSERT_FALSE(Traced(AbilityTraceEvent::Refused).empty());
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused).front().Refusal, AbilityRefusal::NotGranted);
}

TEST_F(AbilityRuntime, TheGrantAndActivateVerbsTakeASource)
{
    const AbilityId read = Define("asset://abilities/read_source.sdata", kReadSource);
    const EntityId actor = Actor();
    const EntityId ring = Entities.CreateEntity();
    Tokens.push_back(Verbs->Bind<&InvokeAbilityGrant>(Verbs->Registry().Find(kAbilityGrantVerb), AbilityVerbs));
    const auto invoke = [&](std::string_view verb, bool keyed) {
        VerbBindingDesc desc;
        desc.Key = std::string(verb) + ".test";
        desc.KeyId = MakeVerbBindingKey(desc.Key);
        desc.VerbName = std::string(verb);
        desc.Inputs = { "target", "source" };
        const auto argument = [](std::string key, VerbArgumentSource from, std::string text) {
            VerbBindingArgument made;
            made.Key = std::move(key);
            made.Source = from;
            made.Text = std::move(text);
            return made;
        };
        desc.Arguments = { argument("target", VerbArgumentSource::Input, "target"),
                           argument("ability", VerbArgumentSource::DataAsset, "asset://abilities/read_source.sdata"),
                           argument("source", VerbArgumentSource::Input, "source") };
        if (keyed)
        {
            desc.Arguments.push_back(argument("key", VerbArgumentSource::Literal, {}));
            desc.Arguments.back().Literal = JsonValue("ringed");
        }
        CompiledVerbBinding binding;
        std::vector<std::string> errors;
        EXPECT_TRUE(CompileVerbBinding(desc, MakeVerbBindingEnvironment(Entities), binding, errors))
            << (errors.empty() ? "" : errors.front());
        const AuthoredValue inputs[] = { AuthoredValue::Entity(actor), AuthoredValue::Entity(ring) };
        return Verbs->Invoke(binding, std::span(inputs)).Status;
    };

    EXPECT_EQ(invoke(kAbilityGrantVerb, true), VerbAdmission::Accepted);
    ASSERT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, 1u);
    EXPECT_EQ(Entities.TryGet<AbilityGrants>(actor)->Entries[0].Source, ring);
    EXPECT_EQ(invoke(kAbilityActivateVerb, false), VerbAdmission::Accepted);
    Step();
    EXPECT_NE(LiveRecord(actor, read), nullptr);
    EXPECT_TRUE(HasLiveActivationFrom(Entities, actor, ring));
}

constexpr std::string_view kPowered = R"({
    "input_default": { "action": "Input.PrimaryFire" },
    "parameters": [ { "key": "power", "default": 5 } ],
    "phases": [ { "key": "p_fire",
        "enter": [ { "key": "o_mark", "op": "invoke", "verb": "test.mark", "arguments": { "value": { "param": "power" } } } ],
        "then": [ { "key": "t_end", "to": "end" } ] } ] })";

TEST_F(AbilityRuntime, AnAbilityReadsItsGrantsParameterElseItsDefault)
{
    const AbilityId powered = Define("asset://abilities/powered.sdata", kPowered);
    AbilityGrantEntries& entries = Entities.GetResource<AbilityGrantEntries>();
    const EntityId strong = Actor();
    const EntityId plain = Actor();
    (void)GrantAbility(*Entities.TryGet<AbilityGrants>(strong), powered, AbilityGrantKind::Authored,
                       entries.Intern({ .Key = "g", .Ability = powered, .Override = {}, .Parameters = { { "power", 14.0 } } }));
    Grant(plain, powered);

    Step({ Press(kFire), Release(kFire) });
    std::ranges::sort(Marks.Values);
    EXPECT_EQ(Marks.Values, (std::vector<double>{ 5.0, 14.0 }));
}

TEST_F(AbilityRuntime, EntriesThatDifferOnlyInAParameterAreTwoEntries)
{
    const AbilityId powered = Define("asset://abilities/powered.sdata", kPowered);
    AbilityGrantEntries& entries = Entities.GetResource<AbilityGrantEntries>();
    const std::uint16_t fire = entries.Intern({ .Key = "g", .Ability = powered, .Override = {}, .Parameters = { { "power", 10.0 } } });
    const std::uint16_t ember = entries.Intern({ .Key = "g", .Ability = powered, .Override = {}, .Parameters = { { "power", 14.0 } } });
    ASSERT_NE(fire, kAbilityNoGrantEntry);
    ASSERT_NE(ember, kAbilityNoGrantEntry);
    EXPECT_NE(fire, ember);
    EXPECT_NE(entries.WireKey(fire), entries.WireKey(ember));
    const std::uint16_t again = entries.Intern(
        { .Key = "g", .Ability = powered, .Override = {}, .Parameters = { { "power", 10.0 } } });
    EXPECT_EQ(again, fire);
}

TEST_F(AbilityRuntime, AGrantParameterItsAbilityDoesNotDeclareIsReported)
{
    std::vector<std::string> lines;
    struct Captured : ILogSink
    {
        std::vector<std::string>* Lines = nullptr;
        void Write(LogLevel, std::string_view, std::string_view message) override { Lines->emplace_back(message); }
    };
    LoggingProvider logging;
    logging.AddSink<Captured>().Lines = &lines;
    Host.Logging = &logging;
    const AbilityId powered = Define("asset://abilities/powered.sdata", kPowered);
    const EntityId actor = Actor();
    (void)GrantAbility(*Entities.TryGet<AbilityGrants>(actor), powered, AbilityGrantKind::Authored,
                       Entities.GetResource<AbilityGrantEntries>().Intern(
                           { .Key = "g", .Ability = powered, .Override = {}, .Parameters = { { "pwer", 14.0 } } }));
    Step({ Press(kFire), Release(kFire) });
    EXPECT_EQ(Marks.Values, (std::vector<double>{ 5.0 })) << "the default stands";
    EXPECT_EQ(std::ranges::count_if(lines, [](const std::string& line) { return line.find("pwer") != std::string::npos; }), 1);
}

TEST_F(AbilityRuntime, AnEffectsGrantHasTheEffectsOwnerAsItsSource)
{
    const AbilityId read = Define("asset://abilities/read_source.sdata", kReadSource);
    EffectDefinition worn{};
    worn.Duration = EffectDuration::Infinite;
    const EffectId ring = Entities.GetResource<EffectRegistry>().Define("asset://effects/ring.sdata", worn);
    DeclareEffectGrants(Entities, ring,
                        { AbilityGrantDeclaration{ .Key = "g_read", .Ability = "asset://abilities/read_source.sdata", .Override = {} } });
    const EntityId actor = Actor();
    const EntityId wornRing = Entities.CreateEntity();
    const EntityId held = ApplyEffect(Entities, actor, ring, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now },
                                      LeaseOwner{ .Entity = wornRing });

    Step({ Press(kAlt) });
    const AbilityGrants& grants = *Entities.TryGet<AbilityGrants>(actor);
    ASSERT_EQ(grants.Count, 1u);
    EXPECT_EQ(grants.Entries[0].Holder, held);
    EXPECT_EQ(grants.Entries[0].Source, wornRing);
    const AbilityRecordLocals* locals = LiveLocals(actor, read);
    ASSERT_NE(locals, nullptr);
    EXPECT_EQ(locals->Values[0].Entity, wornRing);
    EXPECT_TRUE(HasLiveActivationFrom(Entities, actor, wornRing));
    EXPECT_FALSE(HasLiveActivationFrom(Entities, actor, actor));

    Step({ Release(kAlt) });
    EXPECT_FALSE(HasLiveActivationFrom(Entities, actor, wornRing));
}

TEST_F(AbilityRuntime, AnAuthoredGrantHasNoSource)
{
    const AbilityId read = Define("asset://abilities/read_source.sdata", kReadSource);
    const EntityId actor = Actor();
    Grant(actor, read);
    Step({ Press(kAlt) });
    const AbilityRecordLocals* locals = LiveLocals(actor, read);
    ASSERT_NE(locals, nullptr);
    EXPECT_FALSE(locals->Values[0].Entity.IsValid());
}

TEST_F(AbilityRuntime, ASourceHandedToAnOperationNeedsAGuard)
{
    const std::optional<JsonValue> data = JsonParse(R"({
        "phases": [ { "key": "p_go",
            "enter": [ { "key": "o_spend", "op": "invoke", "verb": "effect.apply",
                         "arguments": { "target": { "port": "source" },
                                        "effect": { "data": "asset://effects/ammo_1.sdata" } } } ],
            "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    ASSERT_TRUE(data.has_value());
    AbilityDeclaration declaration;
    std::vector<AssetRef> dependencies;
    std::vector<AbilityDiagnostic> diagnostics;
    ASSERT_TRUE(CompileAbilityDeclaration(*data, declaration, dependencies, diagnostics)) << Describe(diagnostics);
    AbilityDefinition definition;
    EXPECT_FALSE(ResolveAbilityDeclaration(declaration, "asset://abilities/spend.sdata",
                                           MakeAbilityResolveEnvironment(Entities), definition, diagnostics));
    EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const AbilityDiagnostic& d) { return d.Code == "unguarded_recipient"; }))
        << Describe(diagnostics);
}

TEST_F(AbilityRuntime, AStaleTargetSkipsClosed)
{
    const AbilityId sweep = Define("asset://abilities/sweep.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
        "locals": [ { "key": "l_near", "type": "entities" } ],
        "phases": [
          { "key": "p_look", "enter": [ { "key": "o_find", "op": "query", "query": "physics.overlap", "into": "l_near",
                "arguments": { "position": { "port": "aim.origin" }, "radius": { "const": 10.0 }, "ignore": { "port": "actor" } } } ],
            "then": [ { "key": "t_hit", "on": "release", "to": "p_hit" } ] },
          { "key": "p_hit", "enter": [ { "key": "o_damage", "op": "invoke", "verb": "effect.apply", "targets": "l_near",
                "when": { "key": "w_live", "entity": { "of": { "port": "target" }, "has": ["sencha.attribute_set"] } },
                "arguments": { "target": { "port": "target" }, "effect": { "data": "asset://effects/charge_damage.sdata" } } } ],
            "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, sweep);
    const EntityId kept = Target(actor, true);
    const EntityId gone = Target(actor, true);

    Step({ Press(kFire) });
    Entities.DestroyEntity(gone);
    Step({ Release(kFire) });

    EXPECT_FLOAT_EQ(Base(kept, Health), 60.0f);
    const std::vector<AbilityTraceRecord> skipped = Traced(AbilityTraceEvent::Skipped);
    ASSERT_EQ(skipped.size(), 1u);
    EXPECT_EQ(skipped[0].Admission, VerbAdmission::UnresolvedReference) << "skipped for being gone, not for its guard";
}

// A hive's members all answer one death in the same tick.
TEST_F(AbilityRuntime, EveryRequestOfATickIsHeardHoweverManyThereAre)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    AbilityRequestQueue& queue = Entities.GetResource<AbilityRequestQueue>();
    std::vector<EntityId> actors;
    for (int i = 0; i < 300; ++i)
    {
        actors.push_back(Actor());
        Grant(actors.back(), charge, {}, "g_" + std::to_string(i));
        queue.Push({ .Kind = AbilityRequestKind::Activate, .Actor = actors.back(), .Ability = charge });
    }
    Step();
    for (const EntityId actor : actors)
        EXPECT_NE(LiveRecord(actor, charge), nullptr);
}

TEST_F(AbilityRuntime, AFinishedActivationLeavesThePoolWhenItsLingerEnds)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    Grant(actor, charge);
    AbilityRequestQueue& queue = Entities.GetResource<AbilityRequestQueue>();
    const AbilityActivations& pool = Entities.GetResource<AbilityActivations>();
    queue.Push({ .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = charge });
    Step();
    EXPECT_EQ(pool.Stats().Live, 1u);
    queue.Push({ .Kind = AbilityRequestKind::Cancel, .Actor = actor, .Ability = charge });
    Step();
    EXPECT_EQ(pool.Stats().Live, 1u) << "it lingers for presentation";
    Steps(static_cast<int>(Settings.LingerTicks) + 1);
    EXPECT_EQ(pool.Stats().Live, 0u);
}

TEST_F(AbilityRuntime, AnActivationStaysPutWhenItsActorChangesRows)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    Grant(actor, charge);
    Entities.GetResource<AbilityRequestQueue>().Push(
        { .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = charge });
    Step();
    const AbilityRecord* before = LiveRecord(actor, charge);
    ASSERT_NE(before, nullptr);
    const std::uint64_t identity = before->Identity;

    Entities.RemoveComponent<LookOrientation>(actor);
    Entities.AddComponent<LookOrientation>(actor, LookOrientation{});
    ASSERT_TRUE(Entities.MoveEntityToPartition(actor, StoragePartitionId{ 7 }));
    Step();

    EXPECT_EQ(LiveRecord(actor, charge), before) << "a row move carries the handle and leaves the activation";
    EXPECT_EQ(before->Identity, identity);
    EXPECT_EQ(Entities.GetResource<AbilityActivations>().Stats().Live, 1u);
}

TEST_F(AbilityRuntime, ADestroyedActorsActivationsAreCollectedAndItsHandlesGoStale)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    Grant(actor, charge);
    Entities.GetResource<AbilityRequestQueue>().Push(
        { .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = charge });
    Step();
    const ActiveAbilities remembered = *Entities.TryGet<ActiveAbilities>(actor);
    AbilityActivations& pool = Entities.GetResource<AbilityActivations>();
    ASSERT_EQ(pool.Stats().Live, 1u);

    Entities.DestroyEntity(actor);
    Step();
    EXPECT_EQ(pool.Stats().Live, 0u);

    const EntityId next = Actor();
    Grant(next, charge);
    Entities.GetResource<AbilityRequestQueue>().Push(
        { .Kind = AbilityRequestKind::Activate, .Actor = next, .Ability = charge });
    Step();
    ASSERT_EQ(pool.Stats().Live, 1u);
    EXPECT_EQ(std::as_const(pool).Record(remembered, 0).State, AbilityRecordState::Free)
        << "the slot was reused, so the old handle names nothing";
}

TEST_F(AbilityRuntime, AnActorGivenBackItsComponentDoesNotInheritActivations)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    Grant(actor, charge);
    Entities.GetResource<AbilityRequestQueue>().Push(
        { .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = charge });
    Step();
    Entities.RemoveComponent<ActiveAbilities>(actor);
    Entities.AddComponent<ActiveAbilities>(actor, ActiveAbilities{});
    Step();
    EXPECT_EQ(Entities.GetResource<AbilityActivations>().Stats().Live, 0u);
    EXPECT_EQ(LiveRecord(actor, charge), nullptr);
}

TEST_F(AbilityRuntime, WarmedUpCombatStartsActivationsWithoutAllocating)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    std::vector<EntityId> actors;
    for (int i = 0; i < 32; ++i)
    {
        actors.push_back(Actor());
        Grant(actors.back(), charge, {}, "g_" + std::to_string(i));
    }
    AbilityRequestQueue& queue = Entities.GetResource<AbilityRequestQueue>();
    const AbilityActivations& pool = Entities.GetResource<AbilityActivations>();
    const auto round = [&] {
        for (const EntityId actor : actors)
            queue.Push({ .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = charge });
        Step();
        for (const EntityId actor : actors)
            queue.Push({ .Kind = AbilityRequestKind::Cancel, .Actor = actor, .Ability = charge });
        Step();
    };
    for (int i = 0; i < 3 * static_cast<int>(Settings.LingerTicks); ++i)
        round();
    const std::uint64_t warmed = pool.Stats().Allocated;
    for (int i = 0; i < 200; ++i)
        round();
    EXPECT_EQ(pool.Stats().Allocated, warmed);
    EXPECT_GE(pool.Stats().Reused, 200u * actors.size());
}

TEST_F(AbilityRuntime, SpareActivationsStayBoundedAfterABurst)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    std::vector<EntityId> actors;
    AbilityRequestQueue& queue = Entities.GetResource<AbilityRequestQueue>();
    for (int i = 0; i < 400; ++i)
    {
        actors.push_back(Actor());
        Grant(actors.back(), charge, {}, "g_" + std::to_string(i));
        queue.Push({ .Kind = AbilityRequestKind::Activate, .Actor = actors.back(), .Ability = charge });
    }
    Step();
    const AbilityActivations& pool = Entities.GetResource<AbilityActivations>();
    ASSERT_EQ(pool.Stats().Live, 400u);
    for (const EntityId actor : actors)
        Entities.DestroyEntity(actor);
    Step();
    EXPECT_EQ(pool.Stats().Live, 0u);
    EXPECT_EQ(pool.Stats().Spare, AbilityActivations::kRetention.Floor);
}

// One actor's requests name its starts apart, wherever others' fall in the queue.
TEST_F(AbilityRuntime, ARequestsNameDependsOnlyOnItsOwnActor)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const EntityId first = Actor();
    const EntityId second = Actor();
    Grant(first, charge);
    Grant(second, charge);
    AbilityRequestQueue& queue = Entities.GetResource<AbilityRequestQueue>();
    queue.Push({ .Kind = AbilityRequestKind::Activate, .Actor = first, .Ability = charge });
    queue.Push({ .Kind = AbilityRequestKind::Activate, .Actor = second, .Ability = charge });
    Step();
    ASSERT_NE(LiveRecord(first, charge), nullptr);
    ASSERT_NE(LiveRecord(second, charge), nullptr);
    EXPECT_EQ(LiveRecord(first, charge)->Key.Origin, kAbilityRequestOrigin);
    EXPECT_EQ(LiveRecord(second, charge)->Key.Origin, kAbilityRequestOrigin);
}

TEST_F(AbilityRuntime, AFifthLiveRecordIsRefusedForCapacity)
{
    std::vector<AbilityId> waits;
    for (int i = 0; i < 5; ++i)
        waits.push_back(Define("asset://abilities/wait" + std::to_string(i) + ".sdata", R"({
            "phases": [ { "key": "p_wait", "then": [ { "key": "t_end", "on": "timeout", "ticks": 50, "to": "end" } ] } ] })"));
    const EntityId actor = Actor();
    AbilityRequestQueue& queue = Entities.GetResource<AbilityRequestQueue>();
    for (int i = 0; i < 5; ++i)
    {
        Grant(actor, waits[i], {}, "g_" + std::to_string(i));
        queue.Push({ .Kind = AbilityRequestKind::Activate, .Actor = actor, .Ability = waits[i] });
    }
    Step();
    EXPECT_EQ(LiveCount(actor), 4u);
    ASSERT_EQ(Traced(AbilityTraceEvent::Refused).size(), 1u);
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused)[0].Refusal, AbilityRefusal::Capacity);
}

TEST_F(AbilityRuntime, GrantIdsSurviveTheRemovalOfOthers)
{
    const AbilityId charge = Define("asset://abilities/charge.sdata", kCharge);
    const AbilityId primary = Define("asset://abilities/primary.sdata", kInstant);
    const EntityId actor = Actor();
    const AbilityGrantId first = Grant(actor, primary, { .Action = "Input.AltFire" }, "g_first");
    const AbilityGrantId kept = Grant(actor, charge, {}, "g_kept");
    Step({ Press(kFire) });
    ASSERT_NE(LiveRecord(actor, charge), nullptr);
    EXPECT_EQ(LiveRecord(actor, charge)->Grant, kept);

    ASSERT_TRUE(RevokeAbilityGrant(*Entities.TryGet<AbilityGrants>(actor), first));
    EXPECT_EQ(Entities.TryGet<AbilityGrants>(actor)->Entries[0].Id, kept) << "the survivor moved, its id did not";
    const AbilityGrantId later = Grant(actor, primary, { .Action = "Input.AltFire" }, "g_first");
    EXPECT_NE(later, first) << "an id is never handed out twice";
    Step();
    EXPECT_NE(LiveRecord(actor, charge), nullptr) << "its activation kept its grant";
}

TEST_F(AbilityRuntime, ABotSourceReachesItsGrantsThroughTheSameResolution)
{
    const AbilityId primary = Define("asset://abilities/primary.sdata", kInstant);
    const EntityId bot = Actor();
    Entities.TryGet<InputActionSourceRef>(bot)->Source = 5;
    Grant(bot, primary);
    InputActionState& source = Entities.AddResource<InputActionSourceTable>().Open(5, 2);

    Step({ Press(kFire) });
    EXPECT_TRUE(Marks.Values.empty()) << "the local player's press is not the bot's";

    (void)source.BeginTick(Now);
    (void)source.TickEdgeStorage().Append(Press(kFire));
    Step();
    EXPECT_EQ(Marks.Values.size(), 1u);
}

TEST_F(AbilityRuntime, APhasesTimeoutRunsInItsActorsTime)
{
    const AbilityId waiting = Define("asset://abilities/wait.sdata", R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "phases": [ { "key": "p_wait", "then": [ { "key": "t_end", "on": "timeout", "ticks": 10, "to": "end" } ] } ] })");
    const AbilityId steady = Define("asset://abilities/steady.sdata", R"({
        "input_default": { "action": "Input.AltFire" },
        "phases": [ { "key": "p_wait", "timing": { "domain": "simulation" },
            "then": [ { "key": "t_end", "on": "timeout", "ticks": 10, "to": "end" } ] } ] })");
    const EntityId actor = Actor();
    Grant(actor, waiting);
    Grant(actor, steady, {}, "g_steady");
    Entities.AddComponent<ActorTimeLeases>(actor, ActorTimeLeases{});
    ASSERT_TRUE(AcquireActorTimeLease(*Entities.TryGet<ActorTimeLeases>(actor), LeaseOwner{ .Entity = actor },
                                      kActorTimeOne / 2, Now));
    Step({ Press(kFire), Press(kAlt) });
    Steps(10);
    EXPECT_EQ(LiveRecord(actor, steady), nullptr) << "simulation time";
    EXPECT_NE(LiveRecord(actor, waiting), nullptr) << "half way through its own ten ticks";
    Steps(10);
    EXPECT_EQ(LiveRecord(actor, waiting), nullptr);
}

TEST_F(AbilityRuntime, AFullGrantTableSwapsOneEffectsGrantForAnothersOnOneTick)
{
    const EntityId actor = Actor();
    const std::vector<EffectId> granting = DefineGrantingEffects(kAbilityGrantCapacity + 1);
    std::vector<EntityId> held;
    for (std::size_t i = 0; i < kAbilityGrantCapacity; ++i)
        held.push_back(ApplyNow(actor, granting[i]));
    Step();
    ASSERT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, kAbilityGrantCapacity);

    ASSERT_TRUE(RemoveEffect(Entities, held.front()));
    const EntityId newcomer = ApplyNow(actor, granting.back());
    Step();

    EXPECT_FALSE(HoldsGrantOf(actor, held.front())) << "an ended effect's grant outlived the pass after it ended";
    EXPECT_TRUE(HoldsGrantOf(actor, newcomer))
        << "the new effect's grant waited a pass for room the ended effect was still holding";
}

TEST_F(AbilityRuntime, AGrantingEffectIsRefusedWhenAppliedIfItsGrantsWouldNotFit)
{
    const EntityId actor = Actor();
    const std::vector<EffectId> granting = DefineGrantingEffects(kAbilityGrantCapacity + 1);
    std::vector<EntityId> held;
    for (std::size_t i = 0; i + 1 < kAbilityGrantCapacity; ++i)
        held.push_back(ApplyNow(actor, granting[i]));
    Step();
    ASSERT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, kAbilityGrantCapacity - 1);

    // Room for one, asked for twice in one tick: the first reserved it.
    const EntityId first = ApplyNow(actor, granting[kAbilityGrantCapacity - 1]);
    const EntityId second = ApplyNow(actor, granting[kAbilityGrantCapacity]);
    ASSERT_TRUE(first.IsValid());
    EXPECT_FALSE(second.IsValid()) << "refused when applied, not left to fail in the pass";
    Step();
    EXPECT_TRUE(HoldsGrantOf(actor, first));
    EXPECT_TRUE(Traced(AbilityTraceEvent::GrantRefused).empty());
    EXPECT_EQ(Entities.GetResource<AbilityGrantReservations>().Size(), 0u) << "the pass consumed the reservation";
}

// A grant asked for between an effect's apply and its pass must not take the room the effect reserved.
constexpr std::string_view kUse = R"({
    "locals": [ { "key": "l_source", "type": "entity" } ],
    "activation": [ { "key": "act_calm", "tags": { "none": ["State.Stunned"] } } ],
    "phases": [ { "key": "p_drink",
        "enter": [ { "key": "o_read", "op": "set", "local": "l_source", "from": { "port": "source" } } ],
        "then": [ { "key": "t_done", "on": "timeout", "ticks": 3, "to": "end" } ] } ] })";

TEST_F(AbilityRuntime, ATransientGrantLastsExactlyAsLongAsItsActivation)
{
    const AbilityId drink = Define("asset://abilities/drink.sdata", kUse);
    const EntityId actor = Actor();
    const EntityId potion = Entities.CreateEntity();
    const std::uint16_t entry = Entities.GetResource<AbilityGrantEntries>().Intern({ "use", drink, {} });
    Entities.GetResource<AbilityRequestQueue>().Push({ .Kind = AbilityRequestKind::ActivateEntry, .Actor = actor,
                                                       .Ability = drink, .Instigator = {}, .Cause = {},
                                                       .Source = potion, .Entry = entry });
    Step();
    const AbilityGrants& grants = *Entities.TryGet<AbilityGrants>(actor);
    ASSERT_EQ(grants.Count, 1u);
    EXPECT_EQ(grants.Entries[0].Kind, AbilityGrantKind::Transient);
    EXPECT_EQ(grants.Entries[0].Source, potion);
    ASSERT_NE(LiveRecord(actor, drink), nullptr);
    EXPECT_EQ(LiveLocals(actor, drink)->Values[0].Entity, potion);
    EXPECT_TRUE(HasLiveActivationFrom(Entities, actor, potion));

    Steps(4);
    EXPECT_EQ(LiveRecord(actor, drink), nullptr);
    EXPECT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, 0u) << "the grant ended with its activation";
    EXPECT_EQ(Traced(AbilityTraceEvent::Ended).back().EndReason, AbilityEndReason::Completed);
}

TEST_F(AbilityRuntime, ARefusedTransientActivationLeavesNoGrant)
{
    const AbilityId drink = Define("asset://abilities/drink.sdata", kUse);
    const EntityId actor = Actor();
    (void)Entities.TryGet<GameplayTagContainer>(actor)->Grant(Tag("State.Stunned"));
    const std::uint16_t entry = Entities.GetResource<AbilityGrantEntries>().Intern({ "use", drink, {} });
    Entities.GetResource<AbilityRequestQueue>().Push({ .Kind = AbilityRequestKind::ActivateEntry, .Actor = actor,
                                                       .Ability = drink, .Instigator = {}, .Cause = {},
                                                       .Source = Entities.CreateEntity(), .Entry = entry });
    Step();
    EXPECT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, 0u);
    ASSERT_FALSE(Traced(AbilityTraceEvent::Refused).empty());
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused).back().Refusal, AbilityRefusal::ActivationFailed);
}

TEST_F(AbilityRuntime, ATransientActivationFromASourceThatHasGoneIsNotGranted)
{
    const AbilityId drink = Define("asset://abilities/drink.sdata", kUse);
    const EntityId actor = Actor();
    const EntityId potion = Entities.CreateEntity();
    const std::uint16_t entry = Entities.GetResource<AbilityGrantEntries>().Intern({ "use", drink, {} });
    Entities.GetResource<AbilityRequestQueue>().Push({ .Kind = AbilityRequestKind::ActivateEntry, .Actor = actor,
                                                       .Ability = drink, .Instigator = {}, .Cause = {},
                                                       .Source = potion, .Entry = entry });
    Entities.DestroyEntity(potion);
    Step();
    EXPECT_EQ(LiveRecord(actor, drink), nullptr);
    EXPECT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, 0u);
    ASSERT_FALSE(Traced(AbilityTraceEvent::Refused).empty());
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused).back().Refusal, AbilityRefusal::NotGranted);
}

TEST_F(AbilityRuntime, ATransientActivationDoesNotTakeRoomAnEffectReserved)
{
    const std::vector<EffectId> granting = DefineGrantingEffects(kAbilityGrantCapacity);
    const AbilityId drink = Define("asset://abilities/drink.sdata", kUse);
    const EntityId actor = Actor();
    for (std::size_t i = 0; i + 1 < kAbilityGrantCapacity; ++i)
        (void)ApplyNow(actor, granting[i]);
    Step();
    const EntityId last = ApplyNow(actor, granting.back());
    ASSERT_TRUE(last.IsValid());
    const std::uint16_t entry = Entities.GetResource<AbilityGrantEntries>().Intern({ "use", drink, {} });
    Entities.GetResource<AbilityRequestQueue>().Push({ .Kind = AbilityRequestKind::ActivateEntry, .Actor = actor,
                                                       .Ability = drink, .Instigator = {}, .Cause = {},
                                                       .Source = {}, .Entry = entry });
    Step();
    EXPECT_EQ(LiveRecord(actor, drink), nullptr);
    ASSERT_FALSE(Traced(AbilityTraceEvent::Refused).empty());
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused).back().Refusal, AbilityRefusal::Capacity);
    EXPECT_TRUE(HoldsGrantOf(actor, last));
}

TEST_F(AbilityRuntime, AGrantAskedForDoesNotTakeRoomAnEffectReserved)
{
    const std::vector<EffectId> granting = DefineGrantingEffects(kAbilityGrantCapacity);
    (void)Define("asset://abilities/charge.sdata", kCharge);
    const EntityId actor = Actor();
    for (std::size_t i = 0; i + 1 < kAbilityGrantCapacity; ++i)
        (void)ApplyNow(actor, granting[i]);
    Step();
    const EntityId last = ApplyNow(actor, granting.back());
    ASSERT_TRUE(last.IsValid());

    Tokens.push_back(Verbs->Bind<&InvokeAbilityGrant>(Verbs->Registry().Find(kAbilityGrantVerb), AbilityVerbs));
    VerbBindingDesc desc;
    desc.Key = "grant.test";
    desc.KeyId = MakeVerbBindingKey(desc.Key);
    desc.VerbName = std::string(kAbilityGrantVerb);
    desc.Inputs = { "target" };
    VerbBindingArgument target;
    target.Key = "target";
    target.Source = VerbArgumentSource::Input;
    target.Text = "target";
    VerbBindingArgument ability;
    ability.Key = "ability";
    ability.Source = VerbArgumentSource::DataAsset;
    ability.Text = "asset://abilities/charge.sdata";
    VerbBindingArgument key;
    key.Key = "key";
    key.Literal = JsonValue("scripted");
    desc.Arguments = { target, ability, key };
    CompiledVerbBinding binding;
    std::vector<std::string> errors;
    ASSERT_TRUE(CompileVerbBinding(desc, MakeVerbBindingEnvironment(Entities), binding, errors));
    const AuthoredValue input = AuthoredValue::Entity(actor);
    EXPECT_EQ(Verbs->Invoke(binding, std::span(&input, 1)).Status, VerbAdmission::Refused)
        << "the last slot is the reserved effect's";

    Step();
    EXPECT_TRUE(HoldsGrantOf(actor, last));
    EXPECT_TRUE(Traced(AbilityTraceEvent::GrantRefused).empty());
}

TEST_F(AbilityRuntime, AReservationEndsWithAnEffectRemovedBeforeThePass)
{
    const EntityId actor = Actor();
    const std::vector<EffectId> granting = DefineGrantingEffects(kAbilityGrantCapacity + 1);
    for (std::size_t i = 0; i + 1 < kAbilityGrantCapacity; ++i)
        (void)ApplyNow(actor, granting[i]);
    Step();
    const EntityId withdrawn = ApplyNow(actor, granting[kAbilityGrantCapacity - 1]);
    ASSERT_TRUE(RemoveEffect(Entities, withdrawn));
    const EntityId replacement = ApplyNow(actor, granting[kAbilityGrantCapacity]);
    ASSERT_TRUE(replacement.IsValid()) << "a removed effect reserves nothing";
    Step();
    EXPECT_TRUE(HoldsGrantOf(actor, replacement));
    EXPECT_EQ(Entities.GetResource<AbilityGrantReservations>().Size(), 0u);
}

// Only a grant nothing reserved can find no room: one taken after the effect applied.
TEST_F(AbilityRuntime, AnUnreservedEffectGrantThatDoesNotFitIsTracedOnce)
{
    const EntityId actor = Actor();
    const std::vector<EffectId> granting = DefineGrantingEffects(kAbilityGrantCapacity);
    std::vector<EntityId> held;
    for (std::size_t i = 0; i + 1 < kAbilityGrantCapacity; ++i)
        held.push_back(ApplyNow(actor, granting[i]));
    Step();
    const EntityId late = ApplyNow(actor, granting.back());
    ASSERT_TRUE(late.IsValid());
    (void)Grant(actor, Define("asset://abilities/squatter.sdata", kGrantedInstant), {}, "g_squat");
    Steps(3);

    const std::vector<AbilityTraceRecord> refused = Traced(AbilityTraceEvent::GrantRefused);
    ASSERT_EQ(refused.size(), 1u) << "a grant with no room is reported once, not every pass it waits";
    EXPECT_EQ(refused.front().Actor, actor);
    EXPECT_EQ(refused.front().Key.Ability, Entities.GetResource<AbilityRegistry>().Find(
                                               "asset://abilities/granted_" + std::to_string(kAbilityGrantCapacity - 1) + ".sdata"));
    EXPECT_EQ(refused.front().Refusal, AbilityRefusal::Capacity);

    ASSERT_TRUE(RemoveEffect(Entities, held.front()));
    Step();
    EXPECT_TRUE(HoldsGrantOf(actor, late));
    EXPECT_EQ(Traced(AbilityTraceEvent::GrantRefused).size(), 1u);
}

TEST_F(AbilityRuntime, AnEffectGrantTheEntryTableRefusesIsReportedNotDropped)
{
    (void)Define("asset://abilities/charge.sdata", kCharge);
    EffectDefinition weapon{};
    weapon.Duration = EffectDuration::Infinite;
    const EffectId equipped = Entities.GetResource<EffectRegistry>().Define("asset://effects/weapon.sdata", weapon);
    // These two keys share one FNV-1a wire key, so the wire could not tell them apart.
    const std::vector<std::string> refused = DeclareEffectGrants(
        Entities, equipped,
        { AbilityGrantDeclaration{ .Key = "k32728", .Ability = "asset://abilities/charge.sdata", .Override = {} },
          AbilityGrantDeclaration{ .Key = "k261234", .Ability = "asset://abilities/charge.sdata", .Override = {} } });
    EXPECT_EQ(refused, (std::vector<std::string>{ "k261234" }));
    EXPECT_EQ(Entities.GetResource<AbilityEffectGrants>().Of(equipped).size(), 1u);
}

TEST_F(AbilityRuntime, ReloadingAnEffectsGrantBindingReplacesItsGrant)
{
    (void)Define("asset://abilities/charge.sdata", kCharge);
    EffectDefinition weapon{};
    weapon.Duration = EffectDuration::Infinite;
    const EffectId equipped = Entities.GetResource<EffectRegistry>().Define("asset://effects/weapon.sdata", weapon);
    DeclareEffectGrants(Entities, equipped,
                        { AbilityGrantDeclaration{ .Key = "w_charge", .Ability = "asset://abilities/charge.sdata",
                                                   .Override = { .Action = "Input.AltFire" } } });
    const EntityId actor = Actor();
    (void)ApplyNow(actor, equipped);
    Step();
    ASSERT_EQ(Entities.TryGet<AbilityGrants>(actor)->Count, 1u);

    DeclareEffectGrants(Entities, equipped,
                        { AbilityGrantDeclaration{ .Key = "w_charge", .Ability = "asset://abilities/charge.sdata",
                                                   .Override = { .Action = "Input.PrimaryFire" } } });
    Step();

    const AbilityGrants& grants = *Entities.TryGet<AbilityGrants>(actor);
    ASSERT_EQ(grants.Count, 1u) << "the reloaded grant was added beside the one it replaces";
    const AbilityGrantEntry* entry = Entities.GetResource<AbilityGrantEntries>().Find(grants.Entries[0].Entry);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->Override.Action, std::optional<std::string>("Input.PrimaryFire"));
}

// A projected tag has only its projection to write it.
TEST(AbilityDeclarationResolve, AProjectedTagIsNotOwned)
{
    World world;
    RegisterAbilityKit(world);
    GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
    tags.SetProjected(*tags.RegisterTag("movement.grounded"), "movement.support");
    AbilityDeclaration declaration;
    std::vector<AssetRef> dependencies;
    std::vector<AbilityDiagnostic> diagnostics;
    ASSERT_TRUE(CompileAbilityDeclaration(*JsonParse(R"({ "owned_tags": [ "movement.grounded" ],
        "phases": [ { "key": "p_go", "then": [ { "key": "t_end", "to": "end" } ] } ] })"),
                                          declaration, dependencies, diagnostics));
    AbilityDefinition definition;
    EXPECT_FALSE(ResolveAbilityDeclaration(declaration, "asset://abilities/projected.sdata",
                                           MakeAbilityResolveEnvironment(world), definition, diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.back().Code, "tag_projected");
}
