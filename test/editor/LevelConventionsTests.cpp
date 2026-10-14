// The project's level.conventions asset: found once under the content roots,
// read with every default filled, and refused, with the defaults standing,
// when there are several or it does not fit the schema.

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/LevelConventions.h"
#include "document/NavigationCookInput.h"
#include "document/WorldDocument.h"
#include "operations/EditorOperations.h"
#include "operations/LevelQueries.h"
#include "operations/LevelTarget.h"

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/DataSchema.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    class LevelConventionsTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            Root = fs::temp_directory_path()
                / ("sencha_conventions_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            fs::remove_all(Root);
            fs::create_directories(Root / "data");
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }

        void Write(const std::string& name, std::string_view data)
        {
            std::ofstream(Root / "data" / name, std::ios::trunc)
                << R"({"type": "level.conventions", "version": 1, "data": )" << data << "}";
        }

        LevelConventionsLookup Find() const
        {
            const fs::path roots[] = { Root };
            return FindLevelConventions(roots);
        }

        fs::path Root;
    };

    TEST_F(LevelConventionsTest, WithoutAnAssetTheDefaultsStand)
    {
        const LevelConventionsLookup found = Find();
        EXPECT_FALSE(found.AssetPath.has_value());
        EXPECT_TRUE(found.Problems.empty());
        EXPECT_DOUBLE_EQ(found.Values.Find("shell_thickness")->AsNumber(), 0.25);
        EXPECT_DOUBLE_EQ(found.Values.Find("opening_height")->AsNumber(), 2.2);
        EXPECT_EQ(found.Values.Find("wall_material"), nullptr);
    }

    TEST_F(LevelConventionsTest, AnAuthoredAssetIsReadWithTheRestDefaulted)
    {
        Write("conventions.sdata",
              R"({"shell_thickness": 0.5, "wall_material": "asset://materials/dev/gray.smat"})");
        const LevelConventionsLookup found = Find();
        ASSERT_TRUE(found.AssetPath.has_value()) << (found.Problems.empty() ? "" : found.Problems.front());
        EXPECT_EQ(*found.AssetPath, "asset://data/conventions.sdata");
        EXPECT_DOUBLE_EQ(found.Values.Find("shell_thickness")->AsNumber(), 0.5);
        EXPECT_DOUBLE_EQ(found.Values.Find("space_height")->AsNumber(), 3.0);
        ASSERT_NE(found.Values.Find("wall_material"), nullptr);
    }

    TEST_F(LevelConventionsTest, SeveralAssetsOrAnInvalidOneAreProblemsAndTheDefaultsStand)
    {
        Write("a.sdata", R"({"shell_thickness": 0.5})");
        Write("b.sdata", R"({"shell_thickness": 0.75})");
        LevelConventionsLookup found = Find();
        EXPECT_FALSE(found.AssetPath.has_value());
        ASSERT_EQ(found.Problems.size(), 1u);
        EXPECT_DOUBLE_EQ(found.Values.Find("shell_thickness")->AsNumber(), 0.25);

        fs::remove(Root / "data/b.sdata");
        Write("a.sdata", R"({"shell_thickness": -1, "colour": "red"})");
        found = Find();
        EXPECT_FALSE(found.AssetPath.has_value());
        ASSERT_EQ(found.Problems.size(), 1u);
        EXPECT_NE(found.Problems.front().find("shell_thickness"), std::string::npos);
    }

    TEST_F(LevelConventionsTest, TheRegisteredTypeCompilesWhatTheSchemaAccepts)
    {
        DataAssetTypeRegistry types;
        DataSchemaRegistry schemas;
        RegisterLevelConventionsData(types, schemas);
        const DataAssetTypeRegistration* type = types.Find(kLevelConventionsSubtype);
        ASSERT_NE(type, nullptr);
        ASSERT_NE(schemas.Find(kLevelConventionsSubtype), nullptr);

        const JsonValue data = JsonParse(R"({"opening_width": 1.5})").value_or(JsonValue{});
        const DataAssetCompileResult compiled = type->Compile(data);
        ASSERT_TRUE(compiled.IsValid()) << compiled.Error;
        const auto* conventions = static_cast<const LevelConventions*>(compiled.Value.get());
        EXPECT_FLOAT_EQ(conventions->OpeningWidth, 1.5f);
        EXPECT_FLOAT_EQ(conventions->ShellThickness, 0.25f);
    }

    TEST_F(LevelConventionsTest, TheOperationAnswersWithItsSourceAndValues)
    {
        Write("conventions.sdata", R"({"space_height": 4})");
        LoggingProvider logging;
        WorldDocument world(logging);
        world.SetContentRoots({ Root });
        CommandStack commands;
        LevelTarget level(world, commands);
        EditorOperations operations;
        std::vector<std::string> errors;
        ASSERT_TRUE(operations.Register("level", LevelQueryOperations(level), errors));

        const JsonValue answer = operations.Invoke("conventions.get", JsonValue(JsonValue::Object{}));
        ASSERT_EQ(answer.Find("status")->AsString(), "ok") << JsonStringify(answer);
        const JsonValue& data = *answer.Find("data");
        EXPECT_EQ(data.Find("asset")->AsString(), "asset://data/conventions.sdata");
        EXPECT_DOUBLE_EQ(data.Find("values")->Find("space_height")->AsNumber(), 4.0);
    }

    TEST_F(LevelConventionsTest, NavigationSettingsAreFoundThroughTheSameProjectScan)
    {
        std::ofstream(Root / "data/navigation.sdata", std::ios::trunc) << R"({
            "type": "navigation.settings", "version": 1,
            "data": { "profiles": [ { "tag": "navigation.profile.humanoid" } ], "areas": [] } })";
        const NavigationSettingsLookup found = FindNavigationSettings(Root);
        ASSERT_TRUE(found.Settings.has_value());
        EXPECT_EQ(found.Settings->Profiles.front().Name, "navigation.profile.humanoid");
    }
}
