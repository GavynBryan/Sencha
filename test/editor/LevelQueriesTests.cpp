// The level's read-only operations answer with structured, repeatable data a
// client never has to parse prose for, and name entities by persistent id.

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"
#include "document/commands/RenameEntityCommand.h"
#include "operations/EditorOperations.h"
#include "operations/LevelQueries.h"
#include "operations/LevelSubjects.h"

#include <assets/texture/ImageLoader.h>
#include <core/io/FileBytes.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace
{
    class LevelQueriesTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            std::vector<std::string> errors;
            ASSERT_TRUE(Operations.Register("level", LevelQueryOperations(World), errors));
        }

        EntityId Brush(const Vec3d& center, const Vec3d& half, const char* name)
        {
            EditorDocument& document = World.FocusDocument();
            const EntityId entity = document.GetScene().CreateBrush(center, half);
            auto rename = MakeRenameEntityCommand(entity, name, document.GetScene(), document);
            rename->Execute();
            return entity;
        }

        std::string Id(EntityId entity) const
        {
            return PersistentIdText(World.FocusDocument(), entity);
        }

        JsonValue Call(std::string_view name, std::string_view arguments)
        {
            return Operations.Invoke(name, JsonParse(arguments).value_or(JsonValue{}));
        }

        static const JsonValue& Data(const JsonValue& answer)
        {
            static const JsonValue none;
            const JsonValue* data = answer.Find("data");
            EXPECT_NE(data, nullptr) << JsonStringify(answer);
            return data != nullptr ? *data : none;
        }

        static std::string Text(const JsonValue& answer, std::string_view key)
        {
            const JsonValue* value = answer.Find(key);
            return value != nullptr && value->IsString() ? value->AsString() : std::string{};
        }

        LoggingProvider Logging;
        WorldDocument World{ Logging };
        EditorOperations Operations;
    };

    TEST_F(LevelQueriesTest, SummaryCountsAndListsEveryEntity)
    {
        const EntityId hall = Brush({ 0, 0, 0 }, { 4, 2, 4 }, "Hall");
        (void)Brush({ 20, 0, 0 }, { 1, 1, 1 }, "Crate");

        const JsonValue answer = Call("level.summary", "{}");
        ASSERT_EQ(Text(answer, "status"), "ok");
        const JsonValue& data = Data(answer);
        EXPECT_EQ(data.Find("entity_count")->AsNumber(), 2.0);
        EXPECT_EQ(data.Find("brush_count")->AsNumber(), 2.0);
        EXPECT_FALSE(data.Find("truncated")->AsBool());
        EXPECT_DOUBLE_EQ(data.Find("max")->AsArray()[0].AsNumber(), 21.0);

        const JsonValue& first = data.Find("entities")->AsArray().front();
        EXPECT_EQ(Text(first, "id"), Id(hall));
        EXPECT_EQ(Text(first, "name"), "Hall");
        EXPECT_EQ(Text(first, "kind"), "brush");

        const JsonValue limited = Call("level.summary", R"({"limit": 1})");
        EXPECT_EQ(Data(limited).Find("entities")->AsArray().size(), 1u);
        EXPECT_TRUE(Data(limited).Find("truncated")->AsBool());
    }

    TEST_F(LevelQueriesTest, AnswersAreTheSameOnEveryCall)
    {
        (void)Brush({ 0, 0, 0 }, { 4, 2, 4 }, "Hall");
        (void)Brush({ 6, 0, 0 }, { 1, 1, 1 }, "Crate");
        for (std::string_view name : { "level.summary", "query.near" })
        {
            const std::string_view arguments =
                name == "query.near" ? R"({"point": [0, 0, 0], "radius": 50})" : "{}";
            EXPECT_EQ(JsonStringify(Call(name, arguments)), JsonStringify(Call(name, arguments)))
                << name;
        }
    }

    TEST_F(LevelQueriesTest, DescribeResolvesByIdOrNameAndRefusesWithACode)
    {
        const EntityId hall = Brush({ 0, 0, 0 }, { 4, 2, 4 }, "Hall");

        const JsonValue byName = Call("entity.describe", R"({"subject": "@Hall"})");
        ASSERT_EQ(Text(byName, "status"), "ok");
        EXPECT_EQ(Text(*Data(byName).Find("entity"), "id"), Id(hall));
        EXPECT_EQ(Data(byName).Find("brush")->Find("faces")->AsNumber(), 6.0);
        bool sawName = false;
        for (const JsonValue& component : Data(byName).Find("components")->AsArray())
            sawName = sawName || Text(component, "key") == "name";
        EXPECT_TRUE(sawName);

        const std::string byIdArguments = R"({"subject": ")" + Id(hall) + R"("})";
        EXPECT_EQ(Text(Call("entity.describe", byIdArguments), "status"), "ok");

        EXPECT_EQ(Text(Call("entity.describe", R"({"subject": "@Nowhere"})"), "code"),
                  "subject.not_found");
        EXPECT_EQ(Text(Call("entity.describe", R"({"subject": "Hall"})"), "code"),
                  "subject.malformed");

        const EntityId twin = Brush({ 9, 0, 0 }, { 1, 1, 1 }, "Hall");
        const JsonValue ambiguous = Call("entity.describe", R"({"subject": "@Hall"})");
        EXPECT_EQ(Text(ambiguous, "code"), "subject.ambiguous");
        const JsonValue::Array& candidates = ambiguous.Find("detail")->Find("candidates")->AsArray();
        ASSERT_EQ(candidates.size(), 2u);
        EXPECT_EQ(candidates[0].AsString(), Id(hall));
        EXPECT_EQ(candidates[1].AsString(), Id(twin));
    }

    TEST_F(LevelQueriesTest, BoxFindsWhatItTouches)
    {
        const EntityId hall = Brush({ 0, 0, 0 }, { 4, 2, 4 }, "Hall");
        (void)Brush({ 20, 0, 0 }, { 1, 1, 1 }, "Crate");

        const JsonValue answer = Call("query.box", R"({"min": [3, -1, -1], "max": [5, 1, 1]})");
        const JsonValue::Array& entities = Data(answer).Find("entities")->AsArray();
        ASSERT_EQ(entities.size(), 1u);
        EXPECT_EQ(Text(entities.front(), "id"), Id(hall));
    }

    TEST_F(LevelQueriesTest, AnEntityWithAPlaceButNoExtentIsFoundAtItsPosition)
    {
        EditorDocument& document = World.FocusDocument();
        const EntityId spawn = document.GetScene().CreateEntity(Vec3d{ 3, 0, 0 });

        const JsonValue answer = Call("query.near", R"({"point": [0, 0, 0], "radius": 5})");
        const JsonValue::Array& entities = Data(answer).Find("entities")->AsArray();
        ASSERT_EQ(entities.size(), 1u);
        const JsonValue& entity = *entities.front().Find("entity");
        EXPECT_EQ(Text(entity, "id"), Id(spawn));
        EXPECT_DOUBLE_EQ(entity.Find("position")->AsArray()[0].AsNumber(), 3.0);
        EXPECT_TRUE(entity.Find("min")->IsNull());
        EXPECT_DOUBLE_EQ(entities.front().Find("distance")->AsNumber(), 3.0);

        const JsonValue boxed = Call("query.box", R"({"min": [2, -1, -1], "max": [4, 1, 1]})");
        EXPECT_EQ(Data(boxed).Find("entities")->AsArray().size(), 1u);
    }

    TEST_F(LevelQueriesTest, PlanSectionCutsTheBrushesItMeetsAndDrawsThemNorthUp)
    {
        const EntityId wall = Brush({ 0, 1, 0 }, { 4, 1, 0.25f }, "North Wall");
        (void)Brush({ 0, -0.25f, 0 }, { 6, 0.25f, 6 }, "Floor");
        const EntityId spawn = World.FocusDocument().GetScene().CreateEntity(Vec3d{ 0, 0, 3 });

        const std::filesystem::path image =
            std::filesystem::temp_directory_path() / "sencha_plan_section.png";
        std::filesystem::remove(image);
        const JsonValue answer = Call("plan.section",
            R"({"height": 1, "pixels_per_metre": 8, "image": ")" + image.generic_string() + R"("})");
        ASSERT_EQ(Text(answer, "status"), "ok") << JsonStringify(answer);
        const JsonValue& data = Data(answer);

        // The cut at one metre meets the wall, not the floor below it.
        const JsonValue::Array& brushes = data.Find("brushes")->AsArray();
        ASSERT_EQ(brushes.size(), 1u);
        EXPECT_EQ(Text(brushes.front(), "id"), Id(wall));
        const JsonValue& contour = brushes.front().Find("contours")->AsArray().front();
        EXPECT_TRUE(contour.Find("closed")->AsBool());
        EXPECT_EQ(contour.Find("points")->AsArray().size(), 4u);

        const JsonValue::Array& points = data.Find("points")->AsArray();
        ASSERT_EQ(points.size(), 1u);
        EXPECT_EQ(Text(points.front(), "id"), Id(spawn));
        EXPECT_DOUBLE_EQ(points.front().Find("position")->AsArray()[1].AsNumber(), 3.0);

        // Wall 8 m by 0.5 m and the spawn 3 m south, with a metre of margin.
        const JsonValue* written = data.Find("image");
        ASSERT_TRUE(written != nullptr && written->IsObject());
        std::vector<std::byte> png;
        ASSERT_TRUE(ReadFileBytes(image, png));
        const std::optional<Image> decoded = LoadImageFromMemory(
            reinterpret_cast<const std::uint8_t*>(png.data()), static_cast<int>(png.size()), false);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->Width, static_cast<std::uint32_t>(written->Find("width")->AsNumber()));
        EXPECT_EQ(decoded->Width, 80u);
        EXPECT_EQ(decoded->Height, 42u);
        std::filesystem::remove(image);
    }

    TEST_F(LevelQueriesTest, NearIsNearestFirstWithACompassBearing)
    {
        // North is -Z, east is +X.
        const EntityId east = Brush({ 10, 0, 0 }, { 1, 1, 1 }, "East");
        const EntityId north = Brush({ 0, 0, -4 }, { 1, 1, 1 }, "North");
        (void)Brush({ 100, 0, 0 }, { 1, 1, 1 }, "Far");

        const JsonValue answer = Call("query.near", R"({"point": [0, 0, 0], "radius": 12})");
        const JsonValue::Array& entities = Data(answer).Find("entities")->AsArray();
        ASSERT_EQ(entities.size(), 2u);
        EXPECT_EQ(Text(*entities[0].Find("entity"), "id"), Id(north));
        EXPECT_EQ(Text(entities[0], "bearing"), "n");
        EXPECT_DOUBLE_EQ(entities[0].Find("distance")->AsNumber(), 3.0);
        EXPECT_EQ(Text(*entities[1].Find("entity"), "id"), Id(east));
        EXPECT_EQ(Text(entities[1], "bearing"), "e");
    }
}
