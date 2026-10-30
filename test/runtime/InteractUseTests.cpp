#include <gtest/gtest.h>

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityRegistry.h>
#include <app/EngineSchedule.h>
#include <app/EngineVocabulary.h>
#include <app/EngineVocabularyBindings.h>
#include <app/GameContexts.h>
#include <assets/data/DataAssetCache.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <controller/Aim.h>
#include <controller/LookOrientation.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleService.h>
#include <core/json/JsonParser.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <input/InputActionRegistry.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <input/InputActionVocabulary.h>
#include <logic/InteractUse.h>
#include <logic/ResponseSet.h>
#include <logic/Responses.h>
#include <logic/ResponsesSystem.h>
#include <physics/PhysicsQueries.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/components/Collider.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// A conventional Use, the way a game ships one: an input action in the game's
// vocabulary, an ability granted to the pawn that binds that action to an
// invoke of interact.use, and things in the world whose responses answer
// interact.use. Every tick here is the real one -- the ability pass reading the
// tick's edges, the verb casting through the physics world, the response pass --
// and nothing in the engine names the action or the reach.

namespace
{
constexpr float kEyeHeight = 1.6f;

// What a response ran: which binding's `value`, for whom, and the entity its
// optional `who` argument read.
struct Mark
{
    double Value = 0.0;
    EntityId Instigator;
    EntityId Who;
};

struct MarkingVerb
{
    std::vector<Mark>* Marks = nullptr;
    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        Mark mark;
        mark.Instigator = invocation.Instigator;
        if (invocation.Arguments != nullptr)
        {
            (void)invocation.Arguments->TryGetFloat(0, mark.Value);
            (void)invocation.Arguments->TryGetEntity(1, mark.Who);
        }
        Marks->push_back(mark);
        return VerbAdmission::Accepted;
    }
};

// test.ready {target}: true unless `target` is one the test made unready. A query
// condition, so judging it fills the runner's query scratch.
struct ReadyQuery
{
    std::unordered_set<EntityId, EntityIdHash> Unready;
    static AuthoredQueryStatus Evaluate(const ReadyQuery& self, std::span<const AuthoredValue> arguments,
                                        AuthoredValue& result)
    {
        EntityId target;
        if (arguments.empty() || !arguments[0].TryGetEntity(target))
            return AuthoredQueryStatus::InvalidArguments;
        result = AuthoredValue::Bool(!self.Unready.contains(target));
        return AuthoredQueryStatus::Value;
    }
};

struct CapturedLog : ILogSink
{
    std::vector<std::string>* Lines = nullptr;
    void Write(LogLevel, std::string_view, std::string_view message) override { Lines->emplace_back(message); }
};

InputActionEdge Press() { return { 0, InputEdgeKind::Press }; }
InputActionEdge Release() { return { 0, InputEdgeKind::Release }; }

// How a game authors its Use: the action it presses, how far it reaches, and
// optionally a fixed direction instead of the pawn's aim.
struct UseSetup
{
    std::string Action = "use";
    double Range = 2.5;
    std::string Direction; // a JSON vector, or empty for the aim
};

std::string UseAbility(const UseSetup& setup)
{
    std::string arguments = std::format(R"("activator": {{"port": "actor"}}, "range": {{"const": {}}})", setup.Range);
    if (!setup.Direction.empty())
        arguments += std::format(R"(, "direction": {{"const": {}}})", setup.Direction);
    return std::format(R"({{
        "input_default": {{"action": "{}"}},
        "prediction": "none",
        "phases": [{{
            "key": "use", "name": "Use",
            "enter": [{{"key": "use", "op": "invoke", "verb": "interact.use", "arguments": {{{}}}}}],
            "then": [{{"key": "done", "to": "end"}}]
        }}]
    }})",
                       setup.Action, arguments);
}

// A binding answering interact.use by marking `value`, with `policy` spliced in.
std::string UseBinding(double value, std::string_view policy = {}, std::string_view conditions = "[]")
{
    return std::format(R"({{"use": {{"on": "interact.use"{}{}, "sequence": {{"conditions": {},
        "commands": [{{"verb": "test.mark", "arguments": {{"value": {{"const": {}}}}}}}]}}}}}})",
                       policy.empty() ? "" : ", ", policy, conditions, value);
}

struct UseHarness
{
    EngineConfig Config;
    RuntimeFrameLoop Loop;
    DataAssetCache DataAssets;
    ConsoleService Console;
    LoggingProvider Logging;
    WorldComponentSchema Schema = MakeSchema();
    RuntimeWorld Runtime{ Schema };
    InputActionRegistry Actions;
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::unique_ptr<EngineVocabularyBindings> Bindings;
    EngineSchedule Schedule;
    ResponsesSystem* Pass = nullptr;
    std::vector<Mark> Marks;
    MarkingVerb Marking{ &Marks };
    ReadyQuery Ready;
    std::vector<VerbBindingToken> Tokens;
    AuthoredQueryBindingToken ReadyToken;
    EntityId Pawn;
    bool Held = false;
    // Everything the systems log, captured before any of them takes a logger.
    std::vector<std::string> Logged;

    explicit UseHarness(const UseSetup& setup = {})
    {
        Logging.AddSink<CapturedLog>().Lines = &Logged;
        World& world = Runtime.Entities();
        RegisterAbilityKit(world);
        InstallAuthoredVocabulary(world);
        {
            VerbRegistrationScope verbs(*FindVerbRegistry(world), "test");
            VerbDefinition mark;
            mark.Name = "test.mark";
            mark.Arguments.Children.push_back(MakeDataField(DataFieldKind::Float, "value", "Value"));
            DataFieldSchema who = MakeDataField(DataFieldKind::Entity, "who", "Who");
            who.Required = false;
            mark.Arguments.Children.push_back(std::move(who));
            (void)verbs.Declare(std::move(mark));
            AuthoredQueryRegistrationScope queries(*FindAuthoredQueryRegistry(world), "test");
            AuthoredQueryDefinition ready;
            ready.Name = "test.ready";
            ready.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "target", "Target"));
            ready.Result = MakeDataField(DataFieldKind::Bool, "result", "Ready");
            (void)queries.Declare(std::move(ready));
            EXPECT_TRUE(CommitTogether(verbs, queries));
        }
        EXPECT_TRUE(InstallEngineVocabulary(world));

        Actions.Rebuild(std::vector<InputActionDefinition>{ { .Name = setup.Action } });
        world.SetResource(InputActionVocabulary{ &Actions, 1 });
        world.AddResource<InputActionState>().Configure(1);

        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(world));
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(world));
        Bindings = std::make_unique<EngineVocabularyBindings>(world, Loop.GetSimulationClock(), *Verbs);
        Tokens.push_back(Verbs->Bind(Verbs->Registry().Find("test.mark"), Marking));
        ReadyToken = Queries->Bind<&ReadyQuery::Evaluate>(Queries->Registry().Find("test.ready"), std::as_const(Ready));

        RegisterPhysics(Schedule);
        RegisterAbilityKitSystems(Schedule, AbilityHost{ .Verbs = Verbs.get(), .Queries = Queries.get() });
        Pass = &RegisterResponsesSystem(Schedule, world, Loop.GetSimulationClock(), *Verbs, Queries.get(),
                                             nullptr, &DataAssets, Console.Registry(), Logging);
        Schedule.Init();
        Bindings->BindPhysics(Physics(), *Queries);
        Bindings->BindInteraction(Physics(), *Pass, *Verbs);

        Pawn = MakePawn(setup);
    }

    ~UseHarness()
    {
        Schedule.Shutdown();
        Tokens.clear();
        Bindings.reset();
    }

    static WorldComponentSchema MakeSchema()
    {
        WorldComponentSchema schema;
        RegisterEngineRuntimeComponents(schema);
        schema.Seal();
        return schema;
    }

    World& Entities() { return Runtime.Entities(); }
    PhysicsWorld& Physics() { return Schedule.Get<PhysicsStepSystem>()->GetSimulation(); }

    static void Place(World& world, EntityId entity, Vec3d position)
    {
        Transform3f pose = Transform3f::Identity();
        pose.Position = position;
        world.AddComponent<LocalTransform>(entity, LocalTransform{ pose });
        world.AddComponent<WorldTransform>(entity, WorldTransform{ pose });
    }

    // At the origin, eyes up, granted the Use ability the game authored.
    EntityId MakePawn(const UseSetup& setup)
    {
        World& world = Entities();
        const EntityId pawn = world.CreateEntity();
        Place(world, pawn, Vec3d(0.0f, kEyeHeight, 0.0f));
        world.AddComponent<LookOrientation>(pawn, LookOrientation{});
        world.AddComponent<InputActionSourceRef>(pawn, InputActionSourceRef{});
        world.AddComponent<AbilityGrants>(pawn, AbilityGrants{});

        AbilityDeclaration declaration;
        std::vector<AssetRef> dependencies;
        std::vector<AbilityDiagnostic> diagnostics;
        const std::optional<JsonValue> json = JsonParse(UseAbility(setup));
        EXPECT_TRUE(json.has_value());
        EXPECT_TRUE(CompileAbilityDeclaration(*json, declaration, dependencies, diagnostics))
            << (diagnostics.empty() ? "" : diagnostics.front().Message);
        AbilityDefinition definition;
        EXPECT_TRUE(ResolveAbilityDeclaration(declaration, "asset://abilities/use.sdata",
                                              MakeAbilityResolveEnvironment(world), definition, diagnostics))
            << (diagnostics.empty() ? "" : diagnostics.front().Path + " " + diagnostics.front().Message);
        const AbilityId ability =
            world.GetResource<AbilityRegistry>().Define("asset://abilities/use.sdata", std::move(definition));
        const std::uint16_t entry = world.GetResource<AbilityGrantEntries>().Intern({ "use", ability, {} });
        (void)GrantAbility(*world.TryGet<AbilityGrants>(pawn), ability, AbilityGrantKind::Authored, entry);
        return pawn;
    }

    void Look(float yaw, float pitch = 0.0f)
    {
        LookOrientation* look = Entities().TryGet<LookOrientation>(Pawn);
        look->Yaw = yaw;
        look->Pitch = pitch;
    }

    // Where the pawn's eyes look along `yaw`, `distance` away.
    Vec3d Ahead(float yaw, float distance)
    {
        const float yawBefore = std::as_const(Entities()).TryGet<LookOrientation>(Pawn)->Yaw;
        Look(yaw);
        const Aim aim = AimOf(Entities(), Pawn);
        Look(yawBefore);
        return aim.Origin + aim.Direction * distance;
    }

    // A small solid box `at`, answering interact.use with `bindings`.
    EntityId Usable(Vec3d at, std::string_view bindings, bool trigger = false)
    {
        World& world = Entities();
        const EntityId usable = world.CreateEntity();
        Place(world, usable, at);
        Collider collider;
        collider.Shape = CollisionShape::MakeBox(Vec3d(0.2f, 0.2f, 0.2f));
        collider.IsTrigger = trigger;
        world.AddComponent<Collider>(usable, collider);
        world.AddComponent<Responses>(usable, Responses{});
        const std::optional<JsonValue> parsed = JsonParse(bindings);
        EXPECT_TRUE(parsed && SetResponseBindings(world, usable, *parsed)) << bindings;
        return usable;
    }

    // A solid box with no responses at all.
    EntityId Wall(Vec3d at, Vec3d halfExtents)
    {
        World& world = Entities();
        const EntityId wall = world.CreateEntity();
        Place(world, wall, at);
        Collider collider;
        collider.Shape = CollisionShape::MakeBox(halfExtents);
        world.AddComponent<Collider>(wall, collider);
        return wall;
    }

    // One fixed tick in the engine's order, with `edges` as the tick's input on
    // the pawn's source; the action stays held from a press until a release.
    void Step(std::vector<InputActionEdge> edges = {})
    {
        World& world = Entities();
        const FrameZoneView& zones = Runtime.BuildFrameView();
        const FixedSimTime time = Loop.GetSimulationClock().BeginFixedTick();
        InputActionState& state = world.GetResource<InputActionState>();
        std::span<InputActionValue> values = state.BeginTick(time.TickIndex);
        for (const InputActionEdge& edge : edges)
        {
            Held = edge.Kind == InputEdgeKind::Press;
            (void)state.TickEdgeStorage().Append(edge);
        }
        values[0].Flags = Held ? InputActionFlags::Held : InputActionFlags::None;
        FixedLogicContext logic{ .Config = Config, .Runtime = Loop, .Time = time, .Entities = world,
                                 .Partitions = zones.Logic };
        Schedule.RunFixedLogic(logic);
        PhysicsContext physics{ Config, Loop, time, world, zones.Physics };
        Schedule.RunPhysics(physics);
        PropagateTransforms(world, zones.Logic, TransformPropagationDomain::Simulation);
        Runtime.EndFrameView();
        Loop.GetSimulationClock().EndFixedTick();
    }

    void Steps(int count)
    {
        for (int i = 0; i < count; ++i)
            Step();
    }

    // Press and let go, then let the response pass that answers it run.
    void Use()
    {
        Step({ Press() });
        Step({ Release() });
        Steps(2);
    }

    [[nodiscard]] UseCandidate Candidate(float range = 2.5f)
    {
        const PhysicsQueries queries(Physics());
        return FindUseCandidate(*Pass, queries, Pawn, AimOf(Entities(), Pawn), range);
    }

    [[nodiscard]] std::vector<double> Values() const
    {
        std::vector<double> values;
        for (const Mark& mark : Marks)
            values.push_back(mark.Value);
        return values;
    }

    void BecomeClient() { Entities().SetResource(SimulationAuthority{ .Authoritative = false }); }
};

// The terminal straight ahead, the switch half a radian to the side, both two
// metres away: both inside the reach, and both inside any volume an area use
// would have broadcast to.
struct TerminalAndSwitch
{
    static constexpr float kSwitchYaw = 0.5f;
    EntityId Terminal;
    EntityId Switch;

    explicit TerminalAndSwitch(UseHarness& harness)
    {
        Terminal = harness.Usable(harness.Ahead(0.0f, 2.0f), UseBinding(1));
        Switch = harness.Usable(harness.Ahead(kSwitchYaw, 2.0f), UseBinding(2));
        harness.Step(); // the colliders become bodies
    }
};
} // namespace

// --- Discovery ---------------------------------------------------------------

TEST(InteractUse, LookingAtOneOfTwoUsableThingsUsesOnlyThatOne)
{
    UseHarness harness;
    const TerminalAndSwitch scene(harness);

    EXPECT_EQ(harness.Candidate().Target, scene.Terminal);
    harness.Use();
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 1 }));

    harness.Look(TerminalAndSwitch::kSwitchYaw);
    EXPECT_EQ(harness.Candidate().Target, scene.Switch);
    harness.Use();
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 1, 2 }));
    ASSERT_EQ(harness.Marks.size(), 2u);
    EXPECT_EQ(harness.Marks[1].Instigator, harness.Pawn) << "the user is the occurrence's instigator";
}

TEST(InteractUse, LookingAtNothingFindsNothingAndUsesNothing)
{
    UseHarness harness;
    const TerminalAndSwitch scene(harness);
    harness.Look(3.0f); // away from both

    EXPECT_FALSE(harness.Candidate().Target.IsValid());
    harness.Use();
    EXPECT_TRUE(harness.Marks.empty());
    EXPECT_EQ(harness.Pass->Stats().Delivered, 0u);
}

TEST(InteractUse, AWallInFrontOfATerminalIsWhatTheRayStrikes)
{
    UseHarness harness;
    const EntityId terminal = harness.Usable(harness.Ahead(0.0f, 2.0f), UseBinding(1));
    (void)harness.Wall(harness.Ahead(0.0f, 1.0f), Vec3d(1.0f, 1.0f, 0.05f));
    harness.Step();

    EXPECT_FALSE(harness.Candidate().Target.IsValid()) << "nothing behind the first body is considered";
    harness.Use();
    EXPECT_TRUE(harness.Marks.empty());
    (void)terminal;
}

TEST(InteractUse, TriggerVolumesNeitherBlockTheRayNorAnswerIt)
{
    UseHarness harness;
    const EntityId terminal = harness.Usable(harness.Ahead(0.0f, 2.0f), UseBinding(1));
    // A volume between the pawn and the terminal that would answer, were it solid.
    (void)harness.Usable(harness.Ahead(0.0f, 1.0f), UseBinding(9), /*trigger*/ true);
    harness.Step();

    EXPECT_EQ(harness.Candidate().Target, terminal);
    harness.Use();
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 1 }));
}

TEST(InteractUse, SomethingBeyondTheReachIsNotACandidate)
{
    UseHarness harness;
    (void)harness.Usable(harness.Ahead(0.0f, 4.0f), UseBinding(1));
    harness.Step();

    EXPECT_FALSE(harness.Candidate(2.5f).Target.IsValid());
    harness.Use();
    EXPECT_TRUE(harness.Marks.empty());
}

TEST(InteractUse, FindingACandidateDeliversNothing)
{
    UseHarness harness;
    const TerminalAndSwitch scene(harness);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(harness.Candidate().Target, scene.Terminal);
    harness.Steps(2);

    const ResponsesSystem::Counters& stats = harness.Pass->Stats();
    EXPECT_EQ(stats.Delivered, 0u);
    EXPECT_EQ(stats.Admitted, 0u);
    EXPECT_EQ(stats.Encoded, 0u);
    EXPECT_EQ(harness.Pass->Pending(), 0u);
    EXPECT_TRUE(harness.Marks.empty());
}

// --- Input ---------------------------------------------------------------------

TEST(InteractUse, HoldingUseIsOneUse)
{
    UseHarness harness;
    const TerminalAndSwitch scene(harness);

    harness.Step({ Press() });
    harness.Steps(20); // held throughout
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 1 }));

    harness.Step({ Release() });
    harness.Step({ Press() });
    harness.Steps(2);
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 1, 1 })) << "a second press is a second use";
    (void)scene;
}

// --- Eligibility -----------------------------------------------------------------

TEST(InteractUse, ADisabledBindingIsBlockedAndDoesNotRun)
{
    UseHarness harness;
    const EntityId terminal = harness.Usable(harness.Ahead(0.0f, 2.0f), UseBinding(1, R"("enabled": false)"));
    harness.Step();

    const UseCandidate candidate = harness.Candidate();
    EXPECT_EQ(candidate.Target, terminal) << "blocked is still a candidate: a prompt can say so";
    EXPECT_EQ(candidate.Availability.Readiness, ResponseReadiness::Blocked);
    EXPECT_TRUE(candidate.Availability.RuntimeStateKnown);
    harness.Use();
    EXPECT_TRUE(harness.Marks.empty());
}

TEST(InteractUse, AOnceBindingAnswersOnceAndIsBlockedAfter)
{
    UseHarness harness;
    (void)harness.Usable(harness.Ahead(0.0f, 2.0f), UseBinding(1, R"("once": true)"));
    harness.Step();

    EXPECT_EQ(harness.Candidate().Availability.Readiness, ResponseReadiness::Available);
    harness.Use();
    EXPECT_EQ(harness.Candidate().Availability.Readiness, ResponseReadiness::Blocked);
    harness.Use();
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 1 }));
}

TEST(InteractUse, ACoolingBindingIsBlockedUntilItCools)
{
    UseHarness harness;
    (void)harness.Usable(harness.Ahead(0.0f, 2.0f), UseBinding(1, R"("cooldown": 0.5)"));
    harness.Step();

    harness.Use();
    EXPECT_EQ(harness.Candidate().Availability.Readiness, ResponseReadiness::Blocked);
    harness.Use();
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 1 }));

    harness.Steps(60);
    EXPECT_EQ(harness.Candidate().Availability.Readiness, ResponseReadiness::Available);
    harness.Use();
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 1, 1 }));
}

TEST(InteractUse, AFailedConditionIsBlockedAndDoesNotRun)
{
    UseHarness harness;
    const EntityId terminal = harness.Usable(
        harness.Ahead(0.0f, 2.0f),
        UseBinding(1, {}, R"([{"query": "test.ready", "arguments": {"target": {"port": "activator"}}}])"));
    harness.Step();
    harness.Ready.Unready.insert(harness.Pawn);

    EXPECT_EQ(harness.Candidate().Target, terminal);
    EXPECT_EQ(harness.Candidate().Availability.Readiness, ResponseReadiness::Blocked);
    harness.Use();
    EXPECT_TRUE(harness.Marks.empty());

    harness.Ready.Unready.clear();
    EXPECT_EQ(harness.Candidate().Availability.Readiness, ResponseReadiness::Available);
    harness.Use();
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 1 }));
}

TEST(InteractUse, ABindingSwitchedOffAfterAdmissionDoesNotRun)
{
    UseHarness harness;
    const EntityId terminal = harness.Usable(harness.Ahead(0.0f, 2.0f), UseBinding(1));
    harness.Step();

    // The press admits the occurrence; before the pass that would run it, the
    // binding stops answering. Availability said yes; the pass still says no.
    harness.Step({ Press() });
    ASSERT_EQ(harness.Pass->Stats().Admitted, 1u);
    harness.Entities().TryGet<Responses>(terminal)->Disabled = 1u;
    harness.Step({ Release() });
    harness.Steps(2);
    EXPECT_TRUE(harness.Marks.empty());
    EXPECT_GE(harness.Pass->Stats().Gated, 1u);
}

// --- Authority -----------------------------------------------------------------

TEST(InteractUse, OnlyTheAuthorityUses)
{
    UseHarness harness;
    const TerminalAndSwitch scene(harness);
    harness.BecomeClient();

    harness.Use();
    EXPECT_TRUE(harness.Marks.empty());
    EXPECT_EQ(harness.Pass->Stats().Admitted, 0u);

    // Asked directly, the verb refuses rather than delivering on a client.
    AuthoredArguments arguments;
    arguments.Resize(4);
    arguments.Set(0, AuthoredValue::Entity(harness.Pawn));
    arguments.Set(1, AuthoredValue::Float(2.5));
    VerbInvocation invocation;
    invocation.Verb = harness.Verbs->Registry().Find(kInteractUseVerb);
    invocation.Arguments = &arguments;
    InteractUseOperations operations(harness.Entities(), harness.Physics(), *harness.Pass);
    EXPECT_EQ(operations.Use(invocation), VerbAdmission::Refused);
    (void)scene;
}

TEST(InteractUse, TheVerbSaysWhyItDidNotUse)
{
    UseHarness harness;
    (void)harness.Usable(harness.Ahead(0.0f, 2.0f), UseBinding(1));
    harness.Step();
    InteractUseOperations operations(harness.Entities(), harness.Physics(), *harness.Pass);
    const auto use = [&](double range) {
        AuthoredArguments arguments;
        arguments.Resize(4);
        arguments.Set(0, AuthoredValue::Entity(harness.Pawn));
        arguments.Set(1, AuthoredValue::Float(range));
        VerbInvocation invocation;
        invocation.Verb = harness.Verbs->Registry().Find(kInteractUseVerb);
        invocation.Arguments = &arguments;
        return operations.Use(invocation);
    };

    EXPECT_EQ(use(0.0), VerbAdmission::Refused) << "a zero reach is allowed and reaches nothing";
    EXPECT_EQ(use(-1.0), VerbAdmission::InvalidArguments);
    harness.Pass->SetCapacity(1);
    EXPECT_EQ(use(2.5), VerbAdmission::Accepted);
    EXPECT_EQ(use(2.5), VerbAdmission::QueueFull) << "the queue's refusal, not a target's";
}

TEST(InteractUse, AClientDiscoversButNeverConfirms)
{
    UseHarness harness;
    const EntityId terminal = harness.Usable(harness.Ahead(0.0f, 2.0f), UseBinding(1, R"("once": true)"));
    harness.Step();
    // What the authority knows and the client was never told: the binding has run.
    harness.Entities().TryGet<Responses>(terminal)->Consumed = 1u;
    harness.BecomeClient();

    const UseCandidate candidate = harness.Candidate();
    EXPECT_EQ(candidate.Target, terminal) << "the authored binding is on the client's copy";
    EXPECT_FALSE(candidate.Availability.RuntimeStateKnown);
    EXPECT_EQ(candidate.Availability.Readiness, ResponseReadiness::Available)
        << "the client cannot see consumption, and must not pretend to: Available here is provisional";
}

TEST(InteractUse, AClientConditionThatFailsIsProvisionalToo)
{
    UseHarness harness;
    (void)harness.Usable(
        harness.Ahead(0.0f, 2.0f),
        UseBinding(1, {}, R"([{"query": "test.ready", "arguments": {"target": {"port": "activator"}}}])"));
    harness.Step();
    harness.Ready.Unready.insert(harness.Pawn);
    harness.BecomeClient();

    const UseCandidate candidate = harness.Candidate();
    EXPECT_EQ(candidate.Availability.Readiness, ResponseReadiness::Blocked);
    EXPECT_FALSE(candidate.Availability.RuntimeStateKnown) << "a client's false is its own reading, not a verdict";
}

TEST(InteractUse, APressUsesTheAimHeldWhenItsTickRunsNotOneLookedUpBefore)
{
    UseHarness harness;
    const TerminalAndSwitch scene(harness);
    // A lookup made earlier -- what a prompt showed -- decides nothing: the press
    // casts again with the aim held when its tick runs, here the switch's. No
    // client is involved; the client half is AClientDiscoversButNeverConfirms.
    EXPECT_EQ(harness.Candidate().Target, scene.Terminal);
    harness.Look(TerminalAndSwitch::kSwitchYaw);
    harness.Use();
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 2 }));
}

// --- Re-entrancy -------------------------------------------------------------------

TEST(InteractUse, AResponseThatUsesSomethingElseRunsToItsEndAndTheOtherAnswersNextPass)
{
    UseHarness harness;
    const Vec3d switchAt = harness.Ahead(TerminalAndSwitch::kSwitchYaw, 2.0f);
    const Aim eyes = AimOf(harness.Entities(), harness.Pawn);
    const Vec3d toSwitch = (switchAt - eyes.Origin).Normalized();
    // The terminal, used, uses the switch on its user's behalf -- from the
    // user's eyes toward the switch -- and then marks what its own `self` port
    // reads. Judging the switch's binding, which is conditioned on a query,
    // happens while the terminal's run is between its commands: were judging to
    // share the run's ports or runner, the second command would read the
    // switch's ports instead of the terminal's.
    const std::string terminalBindings = std::format(
        R"({{"use": {{"on": "interact.use", "sequence": {{"commands": [
            {{"verb": "interact.use", "arguments": {{"activator": {{"port": "activator"}}, "range": {{"const": 5}},
                "origin": {{"const": [{}, {}, {}]}}, "direction": {{"const": [{}, {}, {}]}}}}}},
            {{"verb": "test.mark", "arguments": {{"value": {{"const": 1}}, "who": {{"port": "self"}}}}}}]}}}}}})",
        eyes.Origin.X, eyes.Origin.Y, eyes.Origin.Z, toSwitch.X, toSwitch.Y, toSwitch.Z);
    const EntityId terminal = harness.Usable(harness.Ahead(0.0f, 2.0f), terminalBindings);
    (void)harness.Usable(switchAt,
                         UseBinding(2, {}, R"([{"query": "test.ready", "arguments": {"target": {"port": "activator"}}}])"));
    harness.Step();

    harness.Step({ Press() });
    EXPECT_TRUE(harness.Marks.empty());
    harness.Step({ Release() }); // the terminal's pass: its use admits the switch's, then it marks
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 1 }));
    ASSERT_EQ(harness.Marks.size(), 1u);
    EXPECT_EQ(harness.Marks[0].Who, terminal) << "the terminal's run read its own ports after the nested judgement";
    harness.Step(); // the switch's pass
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 1, 2 }));
    ASSERT_EQ(harness.Marks.size(), 2u);
    EXPECT_EQ(harness.Marks[1].Instigator, harness.Pawn) << "the nested use is still the user's";
    EXPECT_EQ(harness.Pass->Stats().Failed, 0u);
}

// A sequence is compiled by whichever asks for it first. When that is a lookup
// rather than the pass, its diagnostics must still be reported -- not traded for
// those of whatever the next pass happens to run.
TEST(InteractUse, ASequenceFirstCompiledByALookupStillReportsItsDiagnostics)
{
    UseHarness harness;
    const std::vector<std::string>& lines = harness.Logged;
    (void)harness.Usable(harness.Ahead(0.0f, 2.0f),
                         R"({"use": {"on": "interact.use", "sequence": {"commands": [{"verb": "test.undeclared"}]}}})");
    (void)harness.Usable(harness.Ahead(TerminalAndSwitch::kSwitchYaw, 2.0f), UseBinding(2));
    harness.Step();

    (void)harness.Candidate(); // compiles the broken sequence
    harness.Look(TerminalAndSwitch::kSwitchYaw);
    harness.Use(); // the pass runs the other one
    EXPECT_EQ(harness.Values(), (std::vector<double>{ 2 }));
    std::string logged;
    for (const std::string& line : lines)
        logged += "\n  " + line;
    EXPECT_TRUE(std::ranges::any_of(lines, [](const std::string& line) {
        return line.find("test.undeclared") != std::string::npos;
    })) << "the broken sequence's diagnostics were never reported; logged:" << logged;
}

// --- Vocabulary ----------------------------------------------------------------------

TEST(InteractUse, TheVerbAndTheOccurrenceShareANameInSeparateCatalogs)
{
    UseHarness harness;
    const VerbRegistry& verbs = harness.Verbs->Registry();
    const AuthoredEventRegistry* events = FindAuthoredEventRegistry(std::as_const(harness.Entities()));
    ASSERT_NE(events, nullptr);
    ASSERT_TRUE(verbs.Find(kInteractUseVerb).IsValid());
    ASSERT_TRUE(events->Find(kInteractUseEvent).IsValid());
    EXPECT_EQ(verbs.Get(verbs.Find(kInteractUseVerb))->Arguments.Children.size(), 4u);
    EXPECT_EQ(events->Get(events->Find(kInteractUseEvent))->Instigator, "activator");
}

// --- Two games -------------------------------------------------------------------------

// Two games, one mechanism: each names its own action and reach in its own
// data. The engine names neither.
TEST(InteractUse, TwoGamesConfigureTheirOwnActionAndReach)
{
    UseHarness shortReach({ .Action = "use", .Range = 2.5, .Direction = {} });
    UseHarness longReach({ .Action = "interact.primary", .Range = 6.0, .Direction = {} });
    for (UseHarness* game : { &shortReach, &longReach })
    {
        (void)game->Usable(game->Ahead(0.0f, 4.0f), UseBinding(1));
        game->Step();
        game->Use();
    }
    EXPECT_TRUE(shortReach.Marks.empty()) << "four metres is beyond a 2.5 m reach";
    EXPECT_EQ(longReach.Values(), (std::vector<double>{ 1 }));

    const InputActionRegistry& actions = longReach.Actions;
    EXPECT_TRUE(actions.Find("interact.primary").IsValid());
    EXPECT_FALSE(actions.Find("use").IsValid()) << "the second game has no action named use at all";
}

TEST(InteractUse, AGameMayCastAlongItsOwnDirectionInsteadOfTheAim)
{
    // A game whose Use is not where the pawn looks: its ability names the ray.
    UseHarness facing({ .Action = "use", .Range = 3.0, .Direction = "[0, -1, 0]" });
    const EntityId below = facing.Usable(Vec3d(0.0f, kEyeHeight - 1.0f, 0.0f), UseBinding(7));
    (void)facing.Usable(facing.Ahead(0.0f, 2.0f), UseBinding(1));
    facing.Step();

    facing.Use();
    EXPECT_EQ(facing.Values(), (std::vector<double>{ 7 }));
    (void)below;
}
