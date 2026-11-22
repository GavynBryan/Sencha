#include <abilities/AbilityKit.h>
#include <app/EngineVocabulary.h>
#include <app/VocabularyCommands.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <ecs/WorldComponentSchema.h>
#include <ecs/EntityText.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventoryKit.h>
#include <inventory/InventoryVocabulary.h>
#include <participant/LocalControl.h>
#include <time/SimClock.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
WorldComponentSchema EngineSchema()
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();
    return schema;
}

class ConsoleVocabularyTest : public ::testing::Test
{
protected:
    ConsoleVocabularyTest()
        : Schema(EngineSchema())
        , Runtime(Schema)
    {
        World& world = Entities();
        RegisterAbilityKit(world);
        InstallInventoryVocabulary(world);
        Wearable = *world.GetResource<GameplayTagRegistry>().RegisterTag("Item.Wearable");
        (void)world.GetResource<ItemRegistry>().Define("asset://items/arrow.sdata",
                                                       ItemDefinition{ .Name = "Arrow", .StackMax = 20 });
        Hero = world.CreateEntity();
        world.AddComponent<ItemContainer>(Hero, ItemContainer{});
        (void)SetLocalControlSubject(world, Hero);

        InstallAuthoredVocabulary(world);
        EXPECT_TRUE(InstallEngineVocabulary(world));
        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(world));
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(world));
        const VerbRegistry& catalog = Verbs->Registry();
        Tokens.push_back(Verbs->Bind<&InvokeInventoryGive>(catalog.Find(kInventoryGiveVerb), Items));
        QueryToken = BindInventoryQuery(*Queries, std::as_const(world));
    }

    World& Entities() { return Runtime.Entities(); }

    ConsoleResult Verb(std::vector<std::string> line)
    {
        return RunVerbCommand(VocabularyConsoleTarget{ .Entities = Entities(), .Verbs = Verbs.get(), .Queries = Queries.get() },
                              line);
    }

    ConsoleResult Query(std::vector<std::string> line)
    {
        return RunQueryCommand(
            VocabularyConsoleTarget{ .Entities = Entities(), .Verbs = Verbs.get(), .Queries = Queries.get() }, line);
    }

    static std::string Said(const ConsoleResult& result)
    {
        std::string text;
        for (const ConsoleOutputEntry& line : result.Output)
            text += line.Text + "\n";
        return text;
    }

    WorldComponentSchema Schema;
    RuntimeWorld Runtime;
    GameplayTagId Wearable;
    EntityId Hero;
    FixedSimulationLoop Clock;
    InventoryOperations Items{ Entities(), Clock };
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::vector<VerbBindingToken> Tokens;
    AuthoredQueryBindingToken QueryToken;
};
} // namespace

TEST_F(ConsoleVocabularyTest, AGivePrintsTheEntryItMade)
{
    const ConsoleResult given = Verb({ "inventory.give", "into=@me", "definition=items/arrow.sdata", "count=2" });
    ASSERT_TRUE(given.Succeeded()) << Said(given);
    const std::vector<EntityId> held = ItemsIn(Entities(), Hero);
    ASSERT_EQ(held.size(), 1u);
    std::string entry = "inventory.give: accepted; entry = entity ";
    AppendEntityText(entry, held[0]);
    EXPECT_EQ(Said(given), entry + "\n");

    const ConsoleResult counted = Query({ "inventory.count", "container=@me" });
    ASSERT_TRUE(counted.Succeeded()) << Said(counted);
    EXPECT_EQ(Said(counted), "inventory.count: 2\n");
}

TEST_F(ConsoleVocabularyTest, ARefusedGivePrintsItsReason)
{
    const EntityId wardrobe = Entities().CreateEntity();
    Entities().AddComponent<ItemContainer>(wardrobe, ItemContainer{ .Accepts = Wearable });
    std::string into = "into=";
    AppendEntityText(into, wardrobe);

    const ConsoleResult refused = Verb({ "inventory.give", into, "definition=asset://items/arrow.sdata" });
    EXPECT_EQ(refused.Status, ConsoleStatus::ExecutionFailed);
    EXPECT_EQ(Said(refused), "inventory.give: refused: not_accepted\n");
    EXPECT_TRUE(ItemsIn(Entities(), wardrobe).empty());
}

TEST_F(ConsoleVocabularyTest, AKeyTheVerbDoesNotTakeListsTheOnesItDoes)
{
    const ConsoleResult refused = Verb({ "inventory.give", "into=@me", "what=1" });
    EXPECT_EQ(refused.Status, ConsoleStatus::InvalidArguments);
    EXPECT_NE(Said(refused).find("takes no argument 'what'; it takes into, definition, count"), std::string::npos)
        << Said(refused);
    EXPECT_EQ(Query({ "inventory.count", "holder=@me" }).Status, ConsoleStatus::InvalidArguments);
    EXPECT_EQ(Verb({ "inventory.nothing" }).Status, ConsoleStatus::InvalidArguments);
}

TEST_F(ConsoleVocabularyTest, ALeaseIsRefused)
{
    const ConsoleResult refused = Verb({ "movement.burst", "target=@me" });
    EXPECT_EQ(refused.Status, ConsoleStatus::ValidationFailed);
    EXPECT_NE(Said(refused).find("is a lease"), std::string::npos) << Said(refused);
}

TEST_F(ConsoleVocabularyTest, AnAuthorityOnlyVerbIsRefusedOnAClient)
{
    Entities().SetResource(SimulationAuthority{ .Authoritative = false });
    const ConsoleResult refused = Verb({ "inventory.give", "into=@me", "definition=items/arrow.sdata" });
    EXPECT_EQ(refused.Status, ConsoleStatus::ValidationFailed);
    EXPECT_NE(Said(refused).find("runs only on the authority"), std::string::npos) << Said(refused);
    EXPECT_TRUE(ItemsIn(Entities(), Hero).empty());
}
