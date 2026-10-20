#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/TriggerCookValidation.h"

#include <authored/WorldVocabulary.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <logic/TriggerPrograms.h>
#include <world/registry/Registry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

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

TEST_F(TriggerCookValidationTest, LoadsDataThroughAssetSystemForSubtypeCheckingAndReleasesItsLease)
{
    RuntimeAssets assets(Logging, EditorSceneSerializers());
    int compiled = 0;
    ASSERT_TRUE(assets.DataTypes.Register({ .Name = "test.trigger.config", .Compile = [&](const JsonValue&)
    {
        ++compiled;
        DataAssetCompileResult result;
        result.Value = std::make_shared<int>(7);
        return result;
    } }));
    const auto path = std::filesystem::temp_directory_path() / "sencha_trigger_validation_config.sdata";
    struct RemoveFile
    {
        std::filesystem::path Path;
        ~RemoveFile() { std::error_code error; std::filesystem::remove(Path, error); }
    } remove{ path };
    std::ofstream(path) << R"({"type":"test.trigger.config","version":1,"data":{}})";
    ASSERT_TRUE(RegisterAssetFile("asset://data/config.sdata", path, assets.Registry, assets.Assets.Kinds()));
    auto& world = Document->GetScene().GetRegistry().Components;
    VerbRegistrationScope scope(*FindVerbRegistry(world), "test.data");
    VerbDefinition call;
    call.Name = "test.data";
    auto field = MakeDataField(DataFieldKind::DataAssetRef, "config", "Config");
    field.Reference.DataSubtype = "test.trigger.config";
    call.Arguments.Children.push_back(std::move(field));
    (void)scope.Declare(std::move(call));
    ASSERT_TRUE(scope.Commit());
    Program(R"({"version":1,"commands":[{"verb":"test.data","arguments":{"config":{"data":"asset://data/config.sdata"}}}]})");
    EXPECT_FALSE(assets.Assets.IsResident("asset://data/config.sdata", AssetType::Data));
    ASSERT_TRUE(ValidateTriggerProgramsForCook(*Document, &assets, Errors));
    EXPECT_TRUE(Errors.empty());
    EXPECT_EQ(compiled, 1);
    EXPECT_FALSE(assets.Assets.IsResident("asset://data/config.sdata", AssetType::Data));
}
