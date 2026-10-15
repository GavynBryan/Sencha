// Records what the ability kit costs: the bytes each actor carries, the ability
// pass per tick over idle and firing actors, and presenting them. Env-gated;
// scripts/bench_abilities.sh runs it from the profile preset.

#include "BenchRecorder.h"

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityPresentationSystem.h>
#include <abilities/CuePresentationData.h>
#include <abilities/CuePresentationLibrary.h>
#include <app/EngineVocabulary.h>
#include <app/EngineVocabularyBindings.h>
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

#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>

namespace
{
Bench::Recorder Recorder;
constexpr double kTickSeconds = 1.0 / 60.0;

int Reps(int fallback) { return Bench::RepsFromEnvironment("SENCHA_ABILITY_BENCH_REPS", fallback); }

// An automatic: a pulse every second tick while held, each a cue at the aim.
constexpr std::string_view kAutomatic = R"({
    "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
    "phases": [ { "key": "p_fire", "while": { "every": 2, "first": "on_entry",
                    "ops": [ { "key": "o_muzzle", "op": "cue", "cue": "cue.muzzle", "at": { "port": "aim.origin" } } ] },
                  "then": [ { "key": "t_stop", "on": "release", "to": "end" } ] } ] })";

// Every actor granted the automatic, all reading the one local input source.
struct Crowd
{
    explicit Crowd(std::size_t actors)
    {
        RegisterAbilityKit(Entities);
        Entities.RegisterComponent<LocalTransform>();
        Entities.RegisterComponent<LookOrientation>();
        Entities.RegisterComponent<InputActionSourceRef>();
        InstallAuthoredVocabulary(Entities);
        (void)Entities.GetResource<GameplayTagRegistry>().RegisterTag("cue.muzzle");
        EXPECT_TRUE(InstallEngineVocabulary(Entities));
        Actions.Rebuild(std::vector<InputActionDefinition>{ { .Name = "Input.PrimaryFire" } });
        Entities.SetResource(InputActionVocabulary{ &Actions, 1 });
        Entities.AddResource<InputActionState>().Configure(1);
        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(Entities));
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(Entities));
        Bindings = std::make_unique<EngineVocabularyBindings>(Entities, Clock, *Verbs);
        Host = AbilityHost{ .Verbs = Verbs.get(), .Queries = Queries.get() };
        Presenter = std::make_unique<AbilityPresentationSystem>(Verbs.get());

        AbilityDeclaration declaration;
        std::vector<AssetRef> dependencies;
        std::vector<AbilityDiagnostic> diagnostics;
        EXPECT_TRUE(CompileAbilityDeclaration(*JsonParse(kAutomatic), declaration, dependencies, diagnostics));
        AbilityDefinition definition;
        EXPECT_TRUE(ResolveAbilityDeclaration(declaration, "asset://bench/automatic.sdata",
                                              MakeAbilityResolveEnvironment(Entities), definition, diagnostics));
        const AbilityId ability =
            Entities.GetResource<AbilityRegistry>().Define("asset://bench/automatic.sdata", std::move(definition));
        const std::uint16_t entry = Entities.GetResource<AbilityGrantEntries>().Intern({ "g", ability, {} });
        for (std::size_t i = 0; i < actors; ++i)
        {
            const EntityId actor = Entities.CreateEntity();
            Entities.AddComponent<LocalTransform>(actor, LocalTransform{});
            Entities.AddComponent<LookOrientation>(actor, LookOrientation{});
            Entities.AddComponent<InputActionSourceRef>(actor, InputActionSourceRef{});
            Entities.AddComponent<AbilityGrants>(actor, AbilityGrants{});
            (void)GrantAbility(*Entities.TryGet<AbilityGrants>(actor), ability, AbilityGrantKind::Authored, entry);
        }
    }

    void Input(std::vector<InputActionEdge> edges, bool held)
    {
        InputActionState& state = Entities.GetResource<InputActionState>();
        std::span<InputActionValue> values = state.BeginTick(Now);
        for (const InputActionEdge& edge : edges)
            (void)state.TickEdgeStorage().Append(edge);
        values[0].Flags = held ? InputActionFlags::Held : InputActionFlags::None;
    }

    World Entities;
    InputActionRegistry Actions;
    FixedSimulationLoop Clock{ kTickSeconds };
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::unique_ptr<EngineVocabularyBindings> Bindings;
    AbilityHost Host;
    AbilitySettings Settings;
    std::unique_ptr<AbilityPresentationSystem> Presenter;
    std::uint64_t Now = 0;
};

struct TickCost
{
    double Pass = 0.0;
    double Presentation = 0.0;
};

TickCost MedianTickMs(std::size_t actors, bool firing, int reps)
{
    Crowd crowd(actors);
    std::vector<double> pass;
    std::vector<double> presentation;
    for (int tick = 0; tick < 60 + reps; ++tick, ++crowd.Now)
    {
        crowd.Input(firing && tick == 0 ? std::vector<InputActionEdge>{ { 0, InputEdgeKind::Press } }
                                        : std::vector<InputActionEdge>{},
                    firing);
        AdvanceActorTime(crowd.Entities, crowd.Now);
        const auto start = Bench::Clock::now();
        RunAbilityPass(crowd.Entities, crowd.Host, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = crowd.Now },
                       nullptr, crowd.Settings);
        const double passMs = Bench::MillisecondsSince(start);
        const auto presented = Bench::Clock::now();
        crowd.Presenter->Present(crowd.Entities, crowd.Now);
        const double presentMs = Bench::MillisecondsSince(presented);
        if (tick >= 60)
        {
            pass.push_back(passMs);
            presentation.push_back(presentMs);
        }
    }
    return { Bench::Median(pass), Bench::Median(presentation) };
}

void MeasureFootprint()
{
    Recorder.Record("ability.bytes.grants", "bytes", sizeof(AbilityGrants));
    Recorder.Record("ability.bytes.active", "bytes", sizeof(ActiveAbilities));
    Recorder.Record("ability.bytes.locals", "bytes", sizeof(AbilityLocals));
    Recorder.Record("ability.bytes.cue_log", "bytes", sizeof(AbilityCueLog));
    Recorder.Record("ability.bytes.predicted_cues", "bytes", sizeof(AbilityPredictedCues));
    Recorder.Record("ability.bytes.presentation", "bytes", sizeof(AbilityPresentationState));
    Recorder.Record("ability.bytes.actor_time", "bytes", sizeof(ActorTime));
    Recorder.Record("ability.bytes.actor", "bytes",
                    sizeof(AbilityGrants) + sizeof(ActiveAbilities) + sizeof(AbilityLocals) + sizeof(AbilityCueLog)
                        + sizeof(AbilityPredictedCues) + sizeof(AbilityPresentationState) + sizeof(ActorTime));
}

void MeasureTicks()
{
    for (const std::size_t actors : { std::size_t{ 16 }, std::size_t{ 64 }, std::size_t{ 256 } })
    {
        const TickCost idle = MedianTickMs(actors, false, Reps(240));
        const TickCost firing = MedianTickMs(actors, true, Reps(240));
        Recorder.Record(std::format("ability.pass.actors_{}.idle", actors), "ms", idle.Pass);
        Recorder.Record(std::format("ability.pass.actors_{}.firing", actors), "ms", firing.Pass);
        Recorder.Record(std::format("ability.present.actors_{}.firing", actors), "ms", firing.Presentation);
    }
}
}

TEST(AbilityBench, Generate)
{
    const char* out = std::getenv("SENCHA_ABILITY_BENCH_OUT");
    if (out == nullptr)
        GTEST_SKIP() << "set SENCHA_ABILITY_BENCH_OUT to record the ability bench (use scripts/bench_abilities.sh)";
    const std::filesystem::path json(out);
    if (json.has_parent_path())
        std::filesystem::create_directories(json.parent_path());

    Recorder.Clear();
    MeasureFootprint();
    MeasureTicks();

    ASSERT_TRUE(Recorder.WriteJson(json)) << "cannot write " << json.generic_string();
    std::filesystem::path csv = json;
    csv.replace_extension(".csv");
    ASSERT_TRUE(Recorder.WriteCsv(csv)) << "cannot write " << csv.generic_string();
}
