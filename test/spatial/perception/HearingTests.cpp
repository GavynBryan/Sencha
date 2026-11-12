// Hearing reaches every ear within a stimulus's radius times its sensitivity,
// or one observer alone, through the same frame sight reports to; content
// stages a stimulus through perception.stimulus.

#include "PerceptionHarness.h"

#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <perception/HearingSensor.h>
#include <perception/ObservationMemory.h>
#include <perception/ObservationMemorySystem.h>
#include <perception/PerceptionStimuli.h>
#include <world/SimulationAuthority.h>

namespace
{
    struct HearingHarness : PerceptionHarness
    {
        HearingHarness()
        {
            Entities.RegisterComponent<HearingSensor>();
            Entities.RegisterComponent<ObservationMemory>();
        }

        EntityId Ear(const Vec3d& position, float sensitivity = 1.0f)
        {
            const EntityId entity = Entities.CreateEntity();
            Entities.AddComponent<WorldTransform>(entity, WorldTransform{ Transform3f(position, Quatf::Identity(), Vec3d::One()) });
            Entities.AddComponent<HearingSensor>(entity, HearingSensor{ .Sensitivity = sensitivity, .EarOffset = Vec3d::Zero() });
            return entity;
        }

        const Observation* Heard(EntityId observer, EntityId subject) const
        {
            const PerceptionSenses* senses = Entities.TryGetResource<PerceptionSenses>();
            return senses != nullptr ? Frame().Find(observer, senses->Hearing, subject) : nullptr;
        }

        std::size_t HeardCount(EntityId observer) const
        {
            const PerceptionSenses* senses = Entities.TryGetResource<PerceptionSenses>();
            const SenseScan* scan = Frame().ScanOf(observer, senses->Hearing);
            return scan != nullptr ? scan->Count : 0;
        }
    };
}

TEST(Hearing, ReachesEarsWithinTheRadiusTimesTheirSensitivity)
{
    HearingHarness harness;
    const EntityId shooter = harness.Entities.CreateEntity();
    const EntityId near = harness.Ear(Vec3d(5, 0, 0));
    const EntityId far = harness.Ear(Vec3d(15, 0, 0));
    const EntityId keen = harness.Ear(Vec3d(15, 0, 0), 2.0f);
    ASSERT_TRUE(StagePerceptionStimulus(harness.Entities, { .Source = shooter, .Position = Vec3d::Zero(), .Radius = 10.0f }));
    harness.Step();

    const Observation* heard = harness.Heard(near, shooter);
    ASSERT_NE(heard, nullptr);
    EXPECT_EQ(heard->Position, Vec3d::Zero());
    EXPECT_NEAR(heard->Confidence, 0.5f, 1e-5f);
    EXPECT_EQ(harness.Heard(far, shooter), nullptr);
    EXPECT_NE(harness.Heard(keen, shooter), nullptr);
    EXPECT_EQ(harness.Frame().CoverageOf(far, harness.Entities.GetResource<PerceptionSenses>().Hearing),
              ScanCoverage::Complete);
}

TEST(Hearing, AStimulusForOneObserverReachesItAloneWhereverItIs)
{
    HearingHarness harness;
    const EntityId shooter = harness.Entities.CreateEntity();
    const EntityId struck = harness.Ear(Vec3d(100, 0, 0));
    const EntityId bystander = harness.Ear(Vec3d(1, 0, 0));
    ASSERT_TRUE(StagePerceptionStimulus(harness.Entities,
                                        { .Source = shooter, .Position = Vec3d::Zero(), .Radius = 0.0f, .Only = struck }));
    harness.Step();
    ASSERT_NE(harness.Heard(struck, shooter), nullptr);
    EXPECT_FLOAT_EQ(harness.Heard(struck, shooter)->Confidence, 1.0f);
    EXPECT_EQ(harness.Heard(bystander, shooter), nullptr);
}

TEST(Hearing, AnEarDoesNotHearItselfAndAStimulusIsHeardOnce)
{
    HearingHarness harness;
    const EntityId ear = harness.Ear(Vec3d::Zero());
    const EntityId other = harness.Ear(Vec3d(2, 0, 0));
    ASSERT_TRUE(StagePerceptionStimulus(harness.Entities, { .Source = ear, .Position = Vec3d::Zero(), .Radius = 10.0f }));
    harness.Step();
    EXPECT_EQ(harness.HeardCount(ear), 0u);
    EXPECT_EQ(harness.HeardCount(other), 1u);
    harness.Step();
    EXPECT_EQ(harness.HeardCount(other), 0u);
}

TEST(Hearing, AClientStagesNothing)
{
    HearingHarness harness;
    harness.Entities.AddResource<SimulationAuthority>(SimulationAuthority{ .Authoritative = false });
    EXPECT_FALSE(StagePerceptionStimulus(harness.Entities, { .Position = Vec3d::Zero(), .Radius = 10.0f }));
}

TEST(Hearing, ContentStagesThroughTheVerbAndMemoryKeepsWhoWasHeard)
{
    HearingHarness harness;
    InstallAuthoredVocabulary(harness.Entities);
    ASSERT_TRUE(InstallPerceptionVocabulary(harness.Entities));
    VerbDispatcher verbs(*FindVerbRegistry(harness.Entities));
    VerbBindingToken token = BindPerceptionVerbs(verbs, harness.Entities);
    const EntityId shooter = harness.Target(Vec3d(0, 0, 8));
    const EntityId ear = harness.Ear(Vec3d::Zero());
    harness.Entities.AddComponent<ObservationMemory>(ear, ObservationMemory{});

    VerbBindingDesc binding;
    binding.Key = "stimulus";
    binding.KeyId = MakeVerbBindingKey(binding.Key);
    binding.VerbName = std::string(kPerceptionStimulusVerb);
    binding.Inputs = { "source" };
    VerbBindingArgument source;
    source.Key = "source";
    source.Source = VerbArgumentSource::Input;
    source.Text = "source";
    VerbBindingArgument radius;
    radius.Key = "radius";
    radius.Source = VerbArgumentSource::Literal;
    radius.Literal = JsonValue(12.0);
    binding.Arguments = { source, radius };
    CompiledVerbBinding compiled;
    std::vector<std::string> errors;
    ASSERT_TRUE(CompileVerbBinding(binding, VerbBindingEnvironment{ .Verbs = FindVerbRegistry(harness.Entities) },
                                   compiled, errors))
        << (errors.empty() ? "" : errors.front());
    const AuthoredValue who = AuthoredValue::Entity(shooter);
    EXPECT_TRUE(verbs.Invoke(compiled, { &who, 1 }).Accepted());

    ObservationMemorySystem memory;
    harness.Step();
    FixedLogicContext ctx{ .Config = harness.Config, .Runtime = harness.Runtime,
                           .Time = FixedSimTime{ .TickIndex = harness.Tick }, .Entities = harness.Entities,
                           .Partitions = harness.Partitions };
    memory.FixedLogic(ctx);
    const ObservationMemoryEntry* entry = FindRemembered(*harness.Entities.TryGet<ObservationMemory>(ear), shooter);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->Position, Vec3d(0, 0, 8));
    EXPECT_EQ(entry->Sense, harness.Entities.GetResource<PerceptionSenses>().Hearing);
}
