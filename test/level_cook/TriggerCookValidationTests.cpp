#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/TriggerCookValidation.h"

#include <authored/WorldVocabulary.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <logic/TriggerPrograms.h>
#include <world/registry/Registry.h>

#include <gtest/gtest.h>

namespace
{
class TriggerCookValidationTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Document = std::make_unique<EditorDocument>(Logging);
        auto& world = Document->GetScene().GetRegistry().Components;
        VerbRegistrationScope verbs(*FindVerbRegistry(world), "test.trigger_cook");
        VerbDefinition call;
        call.Name = "test.trigger_cook";
        call.Arguments.Children = { MakeDataField(DataFieldKind::Entity, "target", "Target") };
        (void)verbs.Declare(std::move(call));
        ASSERT_TRUE(verbs.Commit());
        Entity = Document->GetScene().CreateEntity({});
        world.AddComponent(Entity, Trigger{});
    }

    void Program(std::string_view text)
    {
        auto source = JsonParse(text);
        ASSERT_TRUE(source);
        ASSERT_TRUE(SetTriggerProgram(Document->GetScene().GetRegistry().Components, Entity, std::move(*source)));
    }

    LoggingProvider Logging;
    std::unique_ptr<EditorDocument> Document;
    EntityId Entity;
    std::vector<std::string> Errors;
};
}

TEST_F(TriggerCookValidationTest, ValidatesInlineProgramsWithoutExecutingGameplay)
{
    Program(R"({"version":1,"commands":[{"verb":"test.trigger_cook","arguments":{"target":{"input":"activator"}}}]})");
    EXPECT_TRUE(ValidateTriggerProgramsForCook(*Document, nullptr, Errors));
    EXPECT_TRUE(Errors.empty());
}

TEST_F(TriggerCookValidationTest, UnknownActiveCommandsRefuseCookWithRowAndEntityLocation)
{
    Program(R"({"version":1,"commands":[{"verb":"game.missing"}]})");
    EXPECT_FALSE(ValidateTriggerProgramsForCook(*Document, nullptr, Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_NE(Errors[0].find("entity "), std::string::npos);
    EXPECT_NE(Errors[0].find("commands[0]"), std::string::npos);
    EXPECT_NE(Errors[0].find("game.missing"), std::string::npos);

    std::string error;
    EXPECT_FALSE(CollectDocumentCookInput(*Document, {}, 16, Logging, nullptr, {}, {}, {}, &error));
    EXPECT_NE(error.find("commands[0]"), std::string::npos);
}

TEST_F(TriggerCookValidationTest, DisabledUnknownRowsRemainLosslessAndDoNotRequireVocabulary)
{
    Program(R"({"version":1,"commands":[{"enabled":false,"verb":"game.missing",
        "arguments":{"future":{"unknown_source":{"asset":"asset://missing.sdata"}}}}]})");
    const auto& world = Document->GetRegistry().Components;
    const JsonValue before = *TriggerProgramSource(world, Entity);
    EXPECT_TRUE(ValidateTriggerProgramsForCook(*Document, nullptr, Errors));
    EXPECT_EQ(JsonStringify(*TriggerProgramSource(world, Entity)), JsonStringify(before));
}

TEST_F(TriggerCookValidationTest, MissingQueriesInvalidArgumentsAndUnknownEligibilityRefuseCook)
{
    for (const auto text : {
        R"({"version":1,"conditions":[{"query":"game.missing"}]})",
        R"({"version":1,"commands":[{"verb":"test.trigger_cook","arguments":{"target":{"const":42}}}]})",
        R"({"version":1,"require_components":["game.missing"]})" })
    {
        Program(text);
        Errors.clear();
        EXPECT_FALSE(ValidateTriggerProgramsForCook(*Document, nullptr, Errors));
        EXPECT_FALSE(Errors.empty());
    }
}

TEST_F(TriggerCookValidationTest, ActivationConfigurationAndMissingAssetEnvironmentRefuseCook)
{
    auto& world = Document->GetScene().GetRegistry().Components;
    ASSERT_NE(world.TryGet<Trigger>(Entity), nullptr);
    world.TryGet<Trigger>(Entity)->StayInterval = 0;
    EXPECT_FALSE(ValidateTriggerProgramsForCook(*Document, nullptr, Errors));
    world.TryGet<Trigger>(Entity)->StayInterval = 0.1f;
    Errors.clear();
    Program(R"({"version":1,"commands":[{"verb":"game.unknown","arguments":{"data":{"data":"asset://test.sdata"}}}]})");
    EXPECT_FALSE(ValidateTriggerProgramsForCook(*Document, nullptr, Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_NE(Errors[0].find("asset environment"), std::string::npos);
}
