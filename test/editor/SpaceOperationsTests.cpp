// The space operations: rooms and doors made by name and position, walled by
// their shells, undoable, and proposable in a draft like every mutation.

#include "WorkspaceFixture.h"

#include "document/BrushComponents.h"
#include "document/BrushCookInput.h"
#include "document/SpaceComponents.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"
#include "viewport/Picking.h"

#include <core/json/JsonParser.h>
#include <filesystem>
#include <fstream>
#include <core/json/JsonStringify.h>
#include <world/registry/Registry.h>

#include <gtest/gtest.h>

namespace
{
class SpaceOperationsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Level, Operations), errors));
    }

    JsonValue Call(std::string_view name, std::string_view arguments)
    {
        return Operations.Invoke(name, JsonParse(arguments).value_or(JsonValue{}));
    }

    JsonValue Ok(std::string_view name, std::string_view arguments)
    {
        JsonValue answer = Call(name, arguments);
        EXPECT_EQ(answer.Find("status")->AsString(), "ok") << name << ": " << JsonStringify(answer);
        const JsonValue* data = answer.Find("data");
        return data != nullptr ? *data : JsonValue{};
    }

    static std::string Code(const JsonValue& answer)
    {
        const JsonValue* code = answer.Find("code");
        return code != nullptr ? code->AsString() : std::string{};
    }

    EntityId Find(std::string_view name)
    {
        const SubjectResolution found = ResolveEntitySubject(Workspace.ActiveDocument(), "@" + std::string(name));
        return found.Resolved() ? found.Entities.front() : EntityId{};
    }

    std::size_t Walls()
    {
        Scene().RefreshDerivedTransforms();
        return CollectCookBrushes(Scene(), AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" }).size();
    }

    LevelTarget Level{ Workspace.World, Commands };
    EditorOperations Operations;
};
}

TEST_F(SpaceOperationsTest, ASpaceIsANamedWalledVolume)
{
    const JsonValue made = Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    EXPECT_EQ(made.Find("space")->Find("name")->AsString(), "Hall");
    EXPECT_EQ(made.Find("created")->AsArray().size(), 1u);
    const EntityId hall = Find("Hall");
    ASSERT_TRUE(hall.IsValid());
    const World& components = Scene().GetRegistry().Components;
    EXPECT_TRUE(components.HasComponent<BrushVolume>(hall));
    EXPECT_TRUE(components.HasComponent<Space>(hall));
    EXPECT_FLOAT_EQ(components.TryGet<Shell>(hall)->Thickness, 0.25f);
    EXPECT_EQ(Walls(), 6u);

    Ok("space.create", R"({"name": "Yard", "min": [20, 0, 0], "max": [24, 3, 4], "shell": false})");
    EXPECT_EQ(Walls(), 6u) << "a space without a shell has no walls";
}

TEST_F(SpaceOperationsTest, ATakenNameOrAFlatBoxIsRefused)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    EXPECT_EQ(Code(Call("space.create", R"({"name": "Hall", "min": [10, 0, 0], "max": [12, 3, 2]})")), "space.name_taken");
    EXPECT_EQ(Code(Call("space.create", R"({"name": "Slit", "min": [10, 0, 0], "max": [12, 0, 2]})")), "space.too_small");
}

TEST_F(SpaceOperationsTest, ADoorBetweenAHallAndACorridorIsCutOnceAndSeenThrough)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("space.create", R"({"name": "Corridor", "min": [8.25, 0, 2], "max": [14, 3, 4]})");
    // The hall's east wall is three slabs, the stretch it shares with the
    // corridor's end and one either side; the shared stretch is built once.
    EXPECT_EQ(Walls(), 13u);

    const JsonValue door = Ok("opening.create", R"({"at": [8.1, 1, 3]})");
    EXPECT_EQ(door.Find("space")->Find("name")->AsString(), "Hall");
    EXPECT_EQ(door.Find("opening")->Find("name")->AsString(), "Hall door");
    Scene().RefreshDerivedTransforms();
    const std::optional<SurfaceHit> hit =
        CastRayAgainstBrushes(Scene(), Ray3d(Vec3d{ 4, 1, 3 }, Vec3d{ 1, 0, 0 }), 50.0f, false);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Point.X, 14.0f, 1e-3f) << "through the door and down the corridor to its far end";

    EXPECT_EQ(Code(Call("opening.create", R"({"at": [30, 1, 30]})")), "opening.no_wall");
}

TEST_F(SpaceOperationsTest, UndoTakesASpaceAway)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Commands.Undo();
    EXPECT_FALSE(Find("Hall").IsValid());
    EXPECT_EQ(Walls(), 0u);
    Commands.Redo();
    EXPECT_TRUE(Find("Hall").IsValid());
}

TEST_F(SpaceOperationsTest, ARoomAndItsDoorAreProposedInADraftAndAcceptedAsOneStep)
{
    Ok("draft.begin", R"({"label": "A hall and a corridor"})");
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("space.create", R"({"name": "Corridor", "min": [8.25, 0, 2], "max": [14, 3, 4]})");
    Ok("opening.create", R"({"at": [8.1, 1, 3]})");
    EXPECT_FALSE(Find("Hall").IsValid()) << "the level has not seen the draft";

    const JsonValue accepted = Ok("draft.accept", "{}");
    EXPECT_EQ(accepted.Find("created")->AsArray().size(), 3u);
    EXPECT_TRUE(Find("Hall").IsValid());
    EXPECT_TRUE(Find("Hall door").IsValid());
    EXPECT_EQ(Walls(), 13u);
    Commands.Undo();
    EXPECT_FALSE(Find("Corridor").IsValid());
    EXPECT_FALSE(Commands.CanUndo());
}

namespace
{
class CarryTest : public SpaceOperationsTest
{
protected:
    void SetUp() override
    {
        SpaceOperationsTest::SetUp();
        Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
        Ok("space.create", R"({"name": "Corridor", "min": [8.25, 0, 2], "max": [14, 3, 4]})");
        Ok("opening.create", R"({"at": [8.1, 1, 3]})");
    }

    Aabb3d VolumeOf(std::string_view name)
    {
        Scene().RefreshDerivedTransforms();
        const EntityId entity = Find(name);
        Aabb3d bounds = Aabb3d::Empty();
        for (const BrushVertex& vertex : Scene().TryGetBrushMesh(entity)->Vertices)
            bounds.ExpandToInclude(Scene().TryGetWorldTransform(entity)->TransformPoint(vertex.Position));
        return bounds;
    }

    std::optional<SurfaceHit> EastFromTheHall(float z = 3.0f)
    {
        Scene().RefreshDerivedTransforms();
        const Aabb3d hall = VolumeOf("Hall");
        return CastRayAgainstBrushes(Scene(), Ray3d(Vec3d{ hall.Min.X + 1.0f, 1, z }, Vec3d{ 1, 0, 0 }), 50.0f, false);
    }
};
}

TEST_F(CarryTest, MovingTheHallAwayStretchesTheCorridorAndTheDoorStillLeadsDownIt)
{
    const JsonValue moved = Ok("space.move", R"({"subject": "@Hall", "offset": [-2, 0, 0]})");
    ASSERT_EQ(moved.Find("carried")->AsArray().size(), 1u);
    EXPECT_EQ(moved.Find("carried")->AsArray().front().Find("name")->AsString(), "Corridor");
    EXPECT_NEAR(VolumeOf("Corridor").Min.X, 6.25f, 1e-3f);
    EXPECT_NEAR(VolumeOf("Corridor").Max.X, 14.0f, 1e-3f);
    const std::optional<SurfaceHit> hit = EastFromTheHall();
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Point.X, 14.0f, 1e-3f);
}

TEST_F(CarryTest, MovingTheHallTowardItShrinksTheCorridor)
{
    Ok("space.move", R"({"subject": "@Hall", "offset": [1.5, 0, 0]})");
    EXPECT_NEAR(VolumeOf("Corridor").Min.X, 9.75f, 1e-3f);
    const std::optional<SurfaceHit> hit = EastFromTheHall();
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Point.X, 14.0f, 1e-3f);
}

TEST_F(CarryTest, ASidewaysMoveCarriesNothing)
{
    const JsonValue moved = Ok("space.move", R"({"subject": "@Hall", "offset": [0, 0, 4]})");
    EXPECT_TRUE(moved.Find("carried")->AsArray().empty());
    EXPECT_NEAR(VolumeOf("Corridor").Min.X, 8.25f, 1e-3f);
}

TEST_F(CarryTest, ADetachedCorridorIsSkippedAndNamed)
{
    Ok("shell.detach", R"({"subject": "@Corridor"})");
    const JsonValue moved = Ok("space.move", R"({"subject": "@Hall", "offset": [-2, 0, 0]})");
    EXPECT_TRUE(moved.Find("carried")->AsArray().empty());
    ASSERT_EQ(moved.Find("skipped")->AsArray().size(), 1u);
    EXPECT_EQ(moved.Find("skipped")->AsArray().front().Find("name")->AsString(), "Corridor");
    EXPECT_NEAR(VolumeOf("Corridor").Min.X, 8.25f, 1e-3f) << "hand-made walls are never edited";
}

TEST_F(CarryTest, OneUndoPutsBothSpacesBack)
{
    Ok("space.move", R"({"subject": "@Hall", "offset": [-2, 0, 0]})");
    Commands.Undo();
    EXPECT_NEAR(VolumeOf("Hall").Min.X, 0.0f, 1e-3f);
    EXPECT_NEAR(VolumeOf("Corridor").Min.X, 8.25f, 1e-3f);
}

TEST_F(CarryTest, AFacePushedOutCarriesItsWalls)
{
    Ok("space.resize_face", R"({"subject": "@Hall", "face": "north", "distance": 2})");
    EXPECT_NEAR(VolumeOf("Hall").Min.Z, -2.0f, 1e-3f);
    EXPECT_EQ(Code(Call("space.resize_face", R"({"subject": "@Hall", "face": "north", "distance": -20})")),
              "space.resize_invalid");
}

TEST_F(CarryTest, AnOpeningSlidesAlongItsWall)
{
    Ok("opening.move", R"({"subject": "@Hall door", "offset": [0, 0, 2.5]})");
    EXPECT_TRUE(EastFromTheHall(3.0f).has_value() && EastFromTheHall(3.0f)->Point.X < 9.0f)
        << "the old doorway is wall again";
}

namespace
{
    std::vector<std::string> Rules(const JsonValue& answer, std::string_view source = {})
    {
        std::vector<std::string> rules;
        for (const JsonValue& record : answer.Find("records")->AsArray())
            if (source.empty() || record.Find("source")->AsString() == source)
                rules.push_back(record.Find("rule")->AsString());
        return rules;
    }

    bool Has(const std::vector<std::string>& rules, std::string_view rule)
    {
        return std::find(rules.begin(), rules.end(), rule) != rules.end();
    }
}

TEST_F(SpaceOperationsTest, OverlappingSpacesAreBothReported)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("space.create", R"({"name": "Annex", "min": [6, 0, 0], "max": [10, 3, 6]})");
    const JsonValue answer = Ok("level.diagnostics", "{}");
    EXPECT_TRUE(Has(Rules(answer, PersistentIdText(Workspace.ActiveDocument(), Find("Hall"))), "space.overlap"));
    EXPECT_TRUE(Has(Rules(answer, PersistentIdText(Workspace.ActiveDocument(), Find("Annex"))), "space.overlap"));
    EXPECT_EQ(Walls(), 0u) << "both shells are refused until they part";
}

TEST_F(SpaceOperationsTest, ADoorLeadingNowhereIsReportedAndOneBetweenSpacesIsNot)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("space.create", R"({"name": "Corridor", "min": [8.25, 0, 2], "max": [14, 3, 4]})");
    Ok("opening.create", R"({"at": [8.1, 1, 3], "name": "Inner door"})");
    Ok("opening.create", R"({"at": [4, 1, -0.1], "name": "Front door"})");
    const JsonValue answer = Ok("level.diagnostics", "{}");
    const std::string inner = PersistentIdText(Workspace.ActiveDocument(), Find("Inner door"));
    const std::string front = PersistentIdText(Workspace.ActiveDocument(), Find("Front door"));
    EXPECT_TRUE(Rules(answer, inner).empty());
    EXPECT_TRUE(Has(Rules(answer, front), "opening.joins_one_space"));
}

TEST_F(SpaceOperationsTest, AnOpeningOverAnotherIsReportedAgainstTheOneRefused)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("opening.create", R"({"at": [4, 1, -0.1], "sill": 1, "width": 1, "height": 1, "name": "First"})");
    Ok("opening.create", R"({"at": [4.3, 1, -0.1], "sill": 1.2, "width": 1, "height": 1, "name": "Second"})");
    const JsonValue answer = Ok("level.diagnostics", "{}");
    const std::vector<std::string> refused =
        Rules(answer, PersistentIdText(Workspace.ActiveDocument(), Find("Second")));
    EXPECT_TRUE(Has(refused, "opening.refused")) << JsonStringify(answer);
}

TEST_F(SpaceOperationsTest, ADoorLowerThanTheNavigationCapsuleIsReported)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("sencha_space_diag_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(root / "data");
    std::ofstream(root / "data/navigation.sdata") << R"({"type": "navigation.settings", "version": 1,
        "data": { "profiles": [ { "tag": "navigation.profile.humanoid", "radius": 0.3, "height": 1.8,
                                  "max_slope_degrees": 45, "max_climb": 0.35, "cell_size": 0.2,
                                  "cell_height": 0.1, "tile_cells": 40 } ], "areas": [] } })";
    Workspace.World.SetContentRoots({ root });

    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("opening.create", R"({"at": [4, 1, -0.1], "height": 1.5, "name": "Hatch"})");
    const JsonValue answer = Ok("level.diagnostics", "{}");
    EXPECT_TRUE(Has(Rules(answer, PersistentIdText(Workspace.ActiveDocument(), Find("Hatch"))), "opening.too_low"));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
