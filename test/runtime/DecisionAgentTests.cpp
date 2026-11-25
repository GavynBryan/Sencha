// A decision agent chooses from its table on its own clock, requests the
// option's ability, and lets a game's input change its mind, with no game.

#include <abilities/AbilityRegistry.h>
#include <abilities/AbilityRequestQueue.h>
#include <app/GameContexts.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/config/EngineConfig.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <logic/decision/DecisionAgentSystem.h>
#include <logic/selection/SelectionTableData.h>
#include <perception/ObservationMemory.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    constexpr std::string_view kTablePath = "asset://data/test/choose.sdata";
    constexpr std::string_view kAct = "asset://abilities/test/act.sdata";
    constexpr std::string_view kIdle = "asset://abilities/test/idle.sdata";

    struct AgentWorld
    {
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers, RuntimeAssets::ReferenceOnly{} };
        std::filesystem::path TableFile;
        EngineConfig Config;
        RuntimeFrameLoop Loop;
        World Entities;
        StoragePartitionSet Partitions;
        DecisionProfile Profile;
        std::optional<DecisionAgents> Agents;
        float Eager = 1.0f;
        std::uint64_t Tick = 0;
        AbilityId Act;
        AbilityId Idle;

        AgentWorld()
        {
            RegisterSelectionTableData(Assets.DataTypes, Assets.DataSchemas);
            TableFile = std::filesystem::temp_directory_path()
                      / ("sencha_decision_" + std::string(testing::UnitTest::GetInstance()->current_test_info()->name())
                         + ".sdata");
            std::ofstream(TableFile, std::ios::trunc) << R"({"type":"selection.table","version":1,"data":{
                "name": "choose", "inputs": ["eager"],
                "options": [
                    { "name": "act", "considerations": [ { "input": "eager", "require": { "min": 1 } } ] },
                    { "name": "idle" } ] }})";
            EXPECT_TRUE(Assets.Registry.Register(AssetRecord{ .Type = AssetType::Data,
                                                              .SourceKind = AssetSourceKind::File,
                                                              .Path = std::string(kTablePath),
                                                              .FilePath = TableFile.generic_string() }));

            Entities.RegisterComponent<DecisionAgent>();
            Entities.RegisterComponent<ObservationMemory>();
            Entities.RegisterComponent<WorldTransform>();
            Entities.RegisterComponent<GameplayTagContainer>();
            Partitions.Add(StoragePartitionId::Default());
            AbilityRegistry& abilities = Entities.AddResource<AbilityRegistry>();
            Act = abilities.Define(kAct, AbilityDefinition{});
            Idle = abilities.Define(kIdle, AbilityDefinition{});
            Entities.AddResource<AbilityRequestQueue>();
            (void)Entities.AddResource<GameplayTagRegistry>().RegisterTag("test.hostile");

            Profile.Table = std::string(kTablePath);
            Profile.Options = { DecisionOptionPlan{ .Option = "act", .Ability = std::string(kAct) },
                                DecisionOptionPlan{ .Option = "idle", .Ability = std::string(kIdle) } };
            Agents.emplace(DecisionAgents::Services{ .Assets = &Assets },
                           DecisionHooks{ .ProfileOf = [this](const World&, EntityId) { return &Profile; } });
            Agents->RegisterInput("eager", [this](const DecisionMoment&) { return Eager; });
        }

        ~AgentWorld()
        {
            std::error_code ec;
            std::filesystem::remove(TableFile, ec);
        }

        EntityId Agent()
        {
            const EntityId entity = Entities.CreateEntity();
            Entities.AddComponent<DecisionAgent>(entity, DecisionAgent{});
            Entities.AddComponent<ObservationMemory>(entity, ObservationMemory{});
            Entities.AddComponent<WorldTransform>(entity, WorldTransform{ Transform3f::Identity() });
            return entity;
        }

        void Step(int ticks = 1)
        {
            for (int i = 0; i < ticks; ++i)
            {
                Entities.GetResource<AbilityRequestQueue>().Pending.clear();
                FixedLogicContext ctx{ .Config = Config, .Runtime = Loop, .Time = FixedSimTime{ .TickIndex = ++Tick },
                                       .Entities = Entities, .Partitions = Partitions };
                Agents->Run(ctx, DecisionRunSettings{});
            }
        }

        [[nodiscard]] bool Requested(AbilityId ability) const
        {
            for (const AbilityRequest& request : Entities.TryGetResource<AbilityRequestQueue>()->Pending)
                if (request.Kind == AbilityRequestKind::Activate && request.Ability == ability)
                    return true;
            return false;
        }
    };
}

TEST(DecisionAgents, ChoosesFromItsTableAndRequestsTheOptionsAbility)
{
    AgentWorld world;
    const EntityId agent = world.Agent();
    bool requested = false;
    for (int tick = 0; tick < 60 && !requested; ++tick)
    {
        world.Step();
        requested = world.Requested(world.Act);
    }
    EXPECT_TRUE(requested) << world.Agents->Describe(agent);
    const DecisionAgent& state = *world.Entities.TryGet<DecisionAgent>(agent);
    EXPECT_EQ(state.Option, 0u);
    EXPECT_EQ(state.Running, world.Act);
    EXPECT_GT(state.Decisions, 0u);
}

TEST(DecisionAgents, AnInputTheGameSuppliesChangesItsMind)
{
    AgentWorld world;
    const EntityId agent = world.Agent();
    world.Step(60);
    ASSERT_EQ(world.Entities.TryGet<DecisionAgent>(agent)->Option, 0u) << world.Agents->Describe(agent);
    world.Eager = 0.0f;
    world.Step(60);
    EXPECT_EQ(world.Entities.TryGet<DecisionAgent>(agent)->Option, 1u) << world.Agents->Describe(agent);
    EXPECT_NE(world.Agents->Describe(agent).find("disqualified by consideration 0"), std::string::npos);
}

TEST(DecisionAgents, DecidesOnlyAsOftenAsItsProfileSays)
{
    AgentWorld world;
    world.Profile.DecisionSeconds = 1.0f;
    const EntityId agent = world.Agent();
    world.Step(60);
    const std::uint32_t inASecond = world.Entities.TryGet<DecisionAgent>(agent)->Decisions;
    EXPECT_GE(inASecond, 1u);
    EXPECT_LE(inASecond, 2u);
}

namespace
{
    EntityId Loaded(AgentWorld& world, const DecisionAgent& saved)
    {
        const EntityId writer = world.Entities.CreateEntity();
        world.Entities.AddComponent<DecisionAgent>(writer, saved);
        ComponentSerializer<DecisionAgent> agents;
        SceneSerializationContext context(world.Logging);
        context.Purpose = SceneRecordPurpose::Save;
        JsonWriteArchive writing;
        EXPECT_TRUE(agents.Save(writing, writer, world.Entities, context));
        const JsonValue record = writing.TakeValue();
        world.Entities.DestroyEntity(writer);
        world.Step(13);

        const EntityId agent = world.Agent();
        JsonReadArchive reading(record);
        EXPECT_TRUE(agents.LoadIntoWorld(reading, agent, world.Entities, context));
        return agent;
    }

    void FollowLastSeen(AgentWorld& world)
    {
        world.Profile.Hostile = "test.hostile";
        world.Profile.Options[0].ToLastSeen = true;
    }
}

TEST(DecisionAgents, AnAgentEngagedWhenSavedHeadsForItsLastContactAfterALoadAndTakesItsTargetAgain)
{
    AgentWorld world;
    FollowLastSeen(world);
    DecisionAgent saved;
    saved.Engaged = true;
    saved.ContactPosition = Vec3d(6.0f, 0.0f, -3.0f);
    saved.SecondsSinceContact = 2.0f;
    const EntityId agent = Loaded(world, saved);

    world.Step(60);
    const DecisionAgent& state = *world.Entities.TryGet<DecisionAgent>(agent);
    ASSERT_TRUE(state.HasDestination) << world.Agents->Describe(agent);
    EXPECT_FLOAT_EQ(state.Destination.X, 6.0f);
    EXPECT_FLOAT_EQ(state.Destination.Z, -3.0f);
    EXPECT_FALSE(state.Target.IsValid());

    const EntityId target = world.Entities.CreateEntity();
    GameplayTagContainer hostile;
    (void)hostile.Grant(world.Entities.GetResource<GameplayTagRegistry>().FindTag("test.hostile"));
    world.Entities.AddComponent<GameplayTagContainer>(target, hostile);
    ObservationMemory& memory = *world.Entities.TryGet<ObservationMemory>(agent);
    memory.Entries[0] = ObservationMemoryEntry{};
    memory.Entries[0].Subject = target;
    memory.Entries[0].Position = Vec3d(8.0f, 0.0f, -3.0f);
    memory.Entries[0].Confidence = 1.0f;
    memory.Count = 1;
    world.Step(60);
    EXPECT_EQ(world.Entities.TryGet<DecisionAgent>(agent)->Target, target) << world.Agents->Describe(agent);
}

TEST(DecisionAgents, AnAgentIdleWhenSavedIsIdleAfterALoad)
{
    AgentWorld world;
    FollowLastSeen(world);
    const EntityId agent = Loaded(world, DecisionAgent{});
    world.Step(60);
    const DecisionAgent& state = *world.Entities.TryGet<DecisionAgent>(agent);
    EXPECT_FALSE(state.Engaged);
    EXPECT_FALSE(state.HasDestination) << world.Agents->Describe(agent);
}
