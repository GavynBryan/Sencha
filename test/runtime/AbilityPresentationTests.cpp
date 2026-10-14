// What abilities do, presented: discrete cues from the actor's log once each, a
// record's pulses as one occurrence, a phase's present entries held while it
// lasts, and a short activation shown through its linger -- only ever through
// cosmetic operations.

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityPresentationSystem.h>
#include <abilities/CuePresentationData.h>
#include <abilities/CuePresentationLibrary.h>
#include <app/EngineVocabulary.h>
#include <app/EngineVocabularyBindings.h>
#include <audio/AudioVerbs.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <controller/LookOrientation.h>
#include <core/json/JsonParser.h>
#include <input/InputActionRegistry.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <input/InputActionVocabulary.h>
#include <time/ActorTimeSystem.h>
#include <time/SimClock.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
constexpr double kTickSeconds = 1.0 / 60.0;
constexpr std::uint8_t kFire = 0;

struct Shown
{
    std::vector<std::pair<std::int64_t, float>> Calls;

    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        std::int64_t count = 0;
        AuthoredVectorValue where;
        (void)invocation.Arguments->TryGetInt(1, count);
        (void)invocation.Arguments->TryGetVector(0, where);
        Calls.emplace_back(count, static_cast<float>(where.Components[0]));
        return VerbAdmission::Accepted;
    }
};

struct Glow
{
    int Held = 0;
    int Acquired = 0;

    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        Held += invocation.Step == LeaseStep::Acquire ? 1 : -1;
        Acquired += invocation.Step == LeaseStep::Acquire ? 1 : 0;
        return VerbAdmission::Accepted;
    }
};

class AbilityPresentation : public ::testing::Test
{
protected:
    AbilityPresentation()
    {
        RegisterAbilityKit(Entities);
        Entities.RegisterComponent<LocalTransform>();
        Entities.RegisterComponent<LookOrientation>();
        Entities.RegisterComponent<InputActionSourceRef>();
        InstallAuthoredVocabulary(Entities);
        GameplayTagRegistry& tags = Entities.GetResource<GameplayTagRegistry>();
        for (const char* name : { "cue.shot", "cue.muzzle", "cue.glow" })
            (void)tags.RegisterTag(name);
        {
            VerbRegistrationScope verbs(*FindVerbRegistry(Entities), "test");
            VerbDefinition show;
            show.Name = "test.show";
            show.Scope = AuthoredScope::Cosmetic;
            show.Prediction = AuthoredPrediction::Presentation;
            DataFieldSchema where = MakeDataField(DataFieldKind::Vector, "where", "Where");
            where.VectorLength = 3;
            where.Required = false;
            DataFieldSchema count = MakeDataField(DataFieldKind::Int, "count", "Count");
            count.Required = false;
            count.Default = std::int64_t{ 0 };
            show.Arguments.Children = { std::move(where), std::move(count) };
            (void)verbs.Declare(std::move(show));
            VerbDefinition glow;
            glow.Name = "test.glow";
            glow.Kind = VerbKind::Lease;
            glow.Scope = AuthoredScope::Cosmetic;
            glow.Prediction = AuthoredPrediction::Presentation;
            (void)verbs.Declare(std::move(glow));
            EXPECT_TRUE(verbs.Commit());
        }
        EXPECT_TRUE(DeclareEngineVocabulary(Entities));
        Actions.Rebuild(std::vector<InputActionDefinition>{ { .Name = "Input.PrimaryFire" } });
        Entities.SetResource(InputActionVocabulary{ &Actions, 1 });
        Entities.AddResource<InputActionState>().Configure(1);

        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(Entities));
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(Entities));
        Bindings = std::make_unique<EngineVocabularyBindings>(Entities, Clock, *Verbs);
        Tokens.push_back(Verbs->Bind(Verbs->Registry().Find("test.show"), Showings));
        Tokens.push_back(Verbs->Bind(Verbs->Registry().Find("test.glow"), Glows));
        Host = AbilityHost{ .Verbs = Verbs.get(), .Queries = Queries.get() };
        Presenter = std::make_unique<AbilityPresentationSystem>(Verbs.get());

        Present(R"({ "cues": [
            { "key": "k_shot", "cue": "cue.shot",
              "once": [ { "verb": "test.show", "arguments": { "where": { "port": "point" } } } ] },
            { "key": "k_muzzle", "cue": "cue.muzzle",
              "once": [ { "verb": "test.show", "arguments": { "count": { "port": "count" } } } ] },
            { "key": "k_glow", "cue": "cue.glow", "sustain": [ { "lease": "test.glow" } ] } ] })");

        Actor = Entities.CreateEntity();
        Entities.AddComponent<LocalTransform>(
            Actor, LocalTransform{ Transform3f{ Vec3d(4.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One() } });
        Entities.AddComponent<LookOrientation>(Actor, LookOrientation{});
        Entities.AddComponent<InputActionSourceRef>(Actor, InputActionSourceRef{});
        Entities.AddComponent<AbilityGrants>(Actor, AbilityGrants{});
    }

    std::vector<std::string> Present(std::string_view json, std::string_view path = "asset://cues/test.sdata")
    {
        CuePresentationDeclaration declaration;
        std::vector<AssetRef> dependencies;
        std::vector<std::string> errors;
        EXPECT_TRUE(CompileCuePresentation(*JsonParse(json), declaration, dependencies, errors));
        Entities.GetResource<CuePresentationLibrary>().Declare(path, declaration, Entities, errors);
        return errors;
    }

    void Grant(std::string_view json)
    {
        AbilityDeclaration declaration;
        std::vector<AssetRef> dependencies;
        std::vector<AbilityDiagnostic> diagnostics;
        ASSERT_TRUE(CompileAbilityDeclaration(*JsonParse(json), declaration, dependencies, diagnostics))
            << diagnostics.front().Message;
        AbilityDefinition definition;
        ASSERT_TRUE(ResolveAbilityDeclaration(declaration, "asset://abilities/a.sdata",
                                              MakeAbilityResolveEnvironment(Entities), definition, diagnostics))
            << diagnostics.front().Message;
        const AbilityId ability =
            Entities.GetResource<AbilityRegistry>().Define("asset://abilities/a.sdata", std::move(definition));
        const std::uint16_t entry = Entities.GetResource<AbilityGrantEntries>().Intern({ "g", ability, {} });
        (void)GrantAbility(*Entities.TryGet<AbilityGrants>(Actor), ability, AbilityGrantKind::Authored, entry);
    }

    // One tick: input, the clock, the ability pass and, unless asked not to, presentation.
    void Step(std::vector<InputActionEdge> edges = {}, bool present = true)
    {
        InputActionState& state = Entities.GetResource<InputActionState>();
        std::span<InputActionValue> values = state.BeginTick(Now);
        for (const InputActionEdge& edge : edges)
        {
            Held = edge.Kind == InputEdgeKind::Press;
            (void)state.TickEdgeStorage().Append(edge);
        }
        values[0].Flags = Held ? InputActionFlags::Held : InputActionFlags::None;
        AdvanceActorTime(Entities, Now);
        RunAbilityPass(Entities, Host, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now }, nullptr,
                       Settings);
        if (present)
            Presenter->Present(Entities, Now);
        ++Now;
    }

    void Steps(int count, bool present = true)
    {
        for (int i = 0; i < count; ++i)
            Step({}, present);
    }

    World Entities;
    InputActionRegistry Actions;
    FixedSimulationLoop Clock{ kTickSeconds };
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::unique_ptr<EngineVocabularyBindings> Bindings;
    std::vector<VerbBindingToken> Tokens;
    Shown Showings;
    Glow Glows;
    AbilityHost Host;
    AbilitySettings Settings;
    std::unique_ptr<AbilityPresentationSystem> Presenter;
    EntityId Actor;
    std::uint64_t Now = 0;
    bool Held = false;
};

constexpr std::string_view kShot = R"({
    "input_default": { "action": "Input.PrimaryFire" },
    "phases": [ { "key": "p_fire", "enter": [ { "key": "o_shot", "op": "cue", "cue": "cue.shot",
                                                 "at": { "port": "aim.origin" } } ],
                  "then": [ { "key": "t_end", "to": "end" } ] } ] })";

constexpr std::string_view kAutomatic = R"({
    "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
    "phases": [ { "key": "p_fire", "while": { "every": 2, "ops": [ { "key": "o_muzzle", "op": "cue", "cue": "cue.muzzle" } ] },
                  "then": [ { "key": "t_stop", "on": "release", "to": "end" } ] } ] })";

constexpr std::string_view kGlowing = R"({
    "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
    "phases": [ { "key": "p_on", "present": [ { "key": "pr_glow", "cue": "cue.glow" } ],
                  "then": [ { "key": "t_off", "on": "release", "to": "end" } ] } ] })";
}

TEST_F(AbilityPresentation, ADiscreteCueIsLoggedAndPresentedOnce)
{
    Grant(kShot);
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    const AbilityCueLog& log = *static_cast<const World&>(Entities).TryGet<AbilityCueLog>(Actor);
    ASSERT_EQ(log.Entries[1].Seq, 1u);
    EXPECT_EQ(log.Entries[1].Cue, Entities.GetResource<GameplayTagRegistry>().FindTag("cue.shot"));
    ASSERT_EQ(Showings.Calls.size(), 1u);
    EXPECT_FLOAT_EQ(Showings.Calls.front().second, 4.0f) << "at the aim's origin";
    Steps(5);
    EXPECT_EQ(Showings.Calls.size(), 1u) << "once";
}

TEST_F(AbilityPresentation, PulsesSinceTheLastLookAreOneOccurrenceWithTheirCount)
{
    Grant(kAutomatic);
    Step({ { kFire, InputEdgeKind::Press } });
    Steps(7, false);
    Step();
    ASSERT_EQ(Showings.Calls.size(), 1u);
    EXPECT_EQ(Showings.Calls.front().first, 4) << "the pulses on ticks 2, 4, 6 and 8";
    Steps(2);
    ASSERT_EQ(Showings.Calls.size(), 2u);
    EXPECT_EQ(Showings.Calls.back().first, 1);
}

TEST_F(AbilityPresentation, APresentEntryIsHeldWhileItsPhaseLasts)
{
    Grant(kGlowing);
    Step({ { kFire, InputEdgeKind::Press } });
    EXPECT_EQ(Glows.Held, 1);
    Steps(5);
    EXPECT_EQ(Glows.Acquired, 1) << "taken once, not every tick";
    Step({ { kFire, InputEdgeKind::Release } });
    EXPECT_EQ(Glows.Held, 0) << "let go when the phase ended";
}

TEST_F(AbilityPresentation, AShortActivationIsPresentedThroughItsLinger)
{
    Grant(R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "phases": [ { "key": "p_burst", "while": { "every": 1, "first": "on_entry",
                                                   "ops": [ { "key": "o_muzzle", "op": "cue", "cue": "cue.muzzle" } ] },
                      "then": [ { "key": "t_end", "to": "end" } ] } ] })");
    Step();
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    ASSERT_EQ(Showings.Calls.size(), 1u) << "it began and ended on one tick, and its pulse still showed";
}

TEST_F(AbilityPresentation, AnActorSeenForTheFirstTimeShowsOnlyRecentCues)
{
    Grant(kShot);
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } }, false);
    Steps(static_cast<int>(kAbilityCueWindowTicks) + 2, false);
    Step();
    EXPECT_TRUE(Showings.Calls.empty()) << "too old to show to someone who just arrived";
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    EXPECT_EQ(Showings.Calls.size(), 1u) << "and new ones show";
}

TEST_F(AbilityPresentation, AGameplayOperationIsNeverPartOfAPresentation)
{
    const std::vector<std::string> errors = Present(R"({ "cues": [
        { "key": "k_bad", "cue": "cue.shot", "once": [ { "verb": "effect.apply", "arguments": {
            "target": { "port": "actor" }, "effect": { "data": "asset://effects/hurt.sdata" } } } ] } ] })",
                                                  "asset://cues/bad.sdata");
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("scope_mismatch"), std::string::npos) << errors.front();
}

TEST_F(AbilityPresentation, AudioWithNoDeviceIsUnavailable)
{
    Present(R"({ "cues": [ { "key": "k_bang", "cue": "cue.shot",
        "once": [ { "verb": "audio.play", "arguments": { "clip": { "asset": "asset://audio/bang.ogg" } } } ] } ] })",
            "asset://cues/audio.sdata");
    Grant(kShot);
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    EXPECT_EQ(Showings.Calls.size(), 1u) << "the other presentations still ran";
}
