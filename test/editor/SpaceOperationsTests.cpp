// The space operations: rooms and doors made by name and position, walled by
// their shells, undoable, and proposable in a draft like every mutation.

#include "WorkspaceFixture.h"

#include "document/BrushComponents.h"
#include "document/BrushCookInput.h"
#include "document/EntityNameComponent.h"
#include "document/SpaceComponents.h"
#include "document/SpaceWalls.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"
#include "meshedit/ManipulationSink.h"
#include "workspace/BrushManipulationSink.h"
#include "document/tools/SpaceTool.h"
#include "tools/ToolRegistry.h"
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
    EXPECT_EQ(made.Find("walls")->Find("created")->AsNumber(), 12.0) << "six walls, each a room side and an outside";
    const EntityId hall = Find("Hall");
    ASSERT_TRUE(hall.IsValid());
    const World& components = Scene().GetRegistry().Components;
    EXPECT_TRUE(components.HasComponent<BrushVolume>(hall));
    EXPECT_TRUE(components.HasComponent<Space>(hall));
    EXPECT_FLOAT_EQ(components.TryGet<Shell>(hall)->Thickness, 0.25f);
    EXPECT_EQ(Walls(), 12u);

    Ok("space.create", R"({"name": "Yard", "min": [20, 0, 0], "max": [24, 3, 4], "shell": false})");
    EXPECT_EQ(Walls(), 12u) << "a space without a shell has no walls";
}

TEST_F(SpaceOperationsTest, ASpaceKeepsItsNameToItselfWhicheverWayANameArrives)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("space.create", R"({"name": "  Atrium\t", "min": [20, 0, 0], "max": [24, 3, 4]})");
    const EntityId atrium = Find("Atrium");
    ASSERT_TRUE(atrium.IsValid()) << "a created space's name is trimmed like a renamed one";
    Ok("brush.box", R"({"min": [40, 0, 0], "max": [41, 1, 1], "name": "Crate"})");

    EXPECT_EQ(Code(Call("entity.rename", R"({"subject": "@Atrium", "name": "Hall"})")), "space.name_taken");
    EXPECT_EQ(Code(Call("entity.rename", R"({"subject": "@Atrium", "name": " Hall "})")), "space.name_taken");
    EXPECT_EQ(Code(Call("entity.rename", R"({"subject": "@Crate", "name": "Hall"})")), "space.name_taken")
        << "an ordinary entity may not take a space's name";
    EXPECT_EQ(Code(Call("entity.rename", R"({"subject": "@Atrium", "name": "Crate"})")), "space.name_taken")
        << "nor a space an ordinary entity's";
    EXPECT_EQ(Code(Call("entity.rename", R"({"subject": "@Atrium", "name": "  "})")), "space.name_invalid");
    EXPECT_EQ(Code(Call("space.create", R"({"name": " Hall", "min": [50, 0, 0], "max": [52, 3, 2]})")), "space.name_taken");
    EXPECT_EQ(Code(Call("space.create", R"({"name": "Crate", "min": [50, 0, 0], "max": [52, 3, 2]})")), "space.name_taken");
    EXPECT_EQ(Code(Call("brush.box", R"({"min": [60, 0, 0], "max": [61, 1, 1], "name": "Hall"})")), "space.name_taken");
    EXPECT_EQ(Code(Call("component.set", R"({"subject": "@Atrium", "component": "name", "field": "value", "value": "\"Hall\""})")),
              "space.name_taken");
    EXPECT_EQ(Code(Call("component.set", R"({"subject": "@Atrium", "component": "name", "field": "value", "value": "\"  \""})")),
              "space.name_invalid") << "component.set names a space as entity.rename does";
    EXPECT_EQ(Ok("component.set", R"({"subject": "@Atrium", "component": "name", "field": "value", "value": "\" Atrium Hall \""})")
                  .Find("stored")->AsString(),
              "\"Atrium Hall\"");
    Ok("entity.rename", R"({"subject": "@Atrium Hall", "name": "Atrium"})");

    Ok("entity.rename", R"({"subject": "@Atrium", "name": " Atrium "})");
    EXPECT_EQ(Find("Atrium"), atrium) << "renaming a space to its own name is no clash";
    Ok("brush.box", R"({"min": [70, 0, 0], "max": [71, 1, 1], "name": "Crate"})");
    EXPECT_FALSE(Find("Crate").IsValid()) << "ordinary entities still share names as before";
    EXPECT_TRUE(Find("Hall").IsValid());
    EXPECT_EQ(Find("Atrium"), atrium);
}

TEST_F(SpaceOperationsTest, AWallsBookkeepingIsTheEditorsNotAnOperations)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    for (const std::string_view field : { "slot", "built", "state" })
        EXPECT_EQ(Code(Call("component.set", R"({"subject": "@Hall north wall", "component": "generated_wall", "field": ")"
                                                 + std::string(field) + R"(", "value": "0"})")),
                  "component.maintained")
            << field;
    EXPECT_EQ(Walls(), 12u);
}

TEST_F(SpaceOperationsTest, ASpaceSharingItsNameIsDiagnosed)
{
    const std::string hall =
        Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})").Find("space")->Find("id")->AsString();
    const EntityId stray = AddBrush({ 20, 0, 0 });
    // The inspector writes names without the operations' check.
    Scene().GetRegistry().Components.TryGet<EntityNameComponent>(stray)->Value = InlineString<64>("Hall");
    const JsonValue answer = Ok("level.diagnostics", "{}");
    bool reported = false;
    for (const JsonValue& record : answer.Find("records")->AsArray())
        reported = reported
            || (record.Find("rule")->AsString() == "space.name_shared" && record.Find("source")->AsString() == hall);
    EXPECT_TRUE(reported) << JsonStringify(answer);
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
    // corridor's end and one either side; the shared stretch is built once,
    // with a side for each room, and every other slab is a room side and an
    // outside.
    EXPECT_EQ(Walls(), 27u);

    const JsonValue door = Ok("opening.create", R"({"at": [8.1, 1, 3]})");
    EXPECT_EQ(door.Find("walls")->Find("modified")->AsNumber(), 3.0)
        << "only the shared wall it cuts: a side for each room and its outside";
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
    EXPECT_EQ(Walls(), 27u);
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

TEST_F(CarryTest, ThePushedWallMovesInTheSameUndoStep)
{
    const auto northWallZ = [this] { return Scene().EvaluatedWorldBounds(Find("Hall north wall"))->Max.Z; };
    const float before = northWallZ();
    const JsonValue pushed = Ok("space.resize_face", R"({"subject": "@Hall", "face": "north", "distance": 2})");
    EXPECT_GT(pushed.Find("walls")->Find("modified")->AsNumber(), 0.0);
    EXPECT_NEAR(northWallZ(), before - 2.0f, 1e-3f);
    Commands.Undo();
    Scene().RefreshDerivedTransforms();
    EXPECT_NEAR(northWallZ(), before, 1e-3f) << "one undo puts the space and its walls back";
    EXPECT_FALSE(SpaceWallsStale(Scene(), Find("Hall")));
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

TEST_F(SpaceOperationsTest, DepsSaysWhatFollowsFromASpaceAndFromItsDoor)
{
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("space.create", R"({"name": "Corridor", "min": [8.25, 0, 2], "max": [14, 3, 4]})");
    Ok("opening.create", R"({"at": [8.1, 1, 3], "name": "Door"})");

    const auto names = [](const JsonValue& list) {
        std::vector<std::string> out;
        for (const JsonValue& item : list.AsArray())
            out.push_back(item.Find("name")->AsString());
        return out;
    };
    const JsonValue hall = Ok("deps", R"({"subject": "@Hall"})");
    EXPECT_GE(hall.Find("walls")->AsNumber(), 6.0);
    EXPECT_EQ(names(*hall.Find("shares_walls_with")), std::vector<std::string>{ "Corridor" });
    EXPECT_EQ(names(*hall.Find("leads_to")), std::vector<std::string>{ "Corridor" });

    const JsonValue corridor = Ok("deps", R"({"subject": "@Corridor"})");
    EXPECT_EQ(names(*corridor.Find("leads_to")), std::vector<std::string>{ "Hall" })
        << "the hall's door leads out of the corridor too";

    const JsonValue door = Ok("deps", R"({"subject": "@Door"})");
    EXPECT_EQ(names(*door.Find("joins")), (std::vector<std::string>{ "Hall", "Corridor" }));
    EXPECT_FALSE(door.Find("cuts")->AsArray().empty());
}

TEST_F(SpaceOperationsTest, AProposalReplaysToTheSameEntitiesWithTheSameIds)
{
    const std::filesystem::path file = std::filesystem::temp_directory_path()
        / ("sencha_wing_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".sproposal");
    Ok("draft.begin", R"({"label": "East wing", "id_seed": 42})");
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("space.create", R"({"name": "Corridor", "min": [8.25, 0, 2], "max": [14, 3, 4]})");
    Ok("opening.create", R"({"at": [8.1, 1, 3]})");
    const JsonValue written = Ok("draft.diff", "{}");
    Ok("draft.save", R"({"path": ")" + file.generic_string() + R"("})");
    Ok("draft.discard", "{}");

    const JsonValue replayed = Ok("draft.open", R"({"path": ")" + file.generic_string() + R"("})");
    EXPECT_EQ(JsonStringify(*replayed.Find("created")), JsonStringify(*written.Find("created")))
        << "the same steps on the same level with the same seed make the same entities";
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

namespace
{
    // What a translate-gizmo drag hands its sink when it lets go.
    void Drag(EditorWorkspace& workspace, EditorScene& scene, std::initializer_list<EntityId> entities, Vec3d offset)
    {
        scene.RefreshDerivedTransforms();
        std::vector<TransformEdit> edits;
        for (const EntityId entity : entities)
        {
            const Transform3f before = *scene.TryGetWorldTransform(entity);
            Transform3f after = before;
            after.Position = after.Position + offset;
            scene.SetWorldTransform(entity, after);
            edits.push_back({ entity, before, after });
        }
        workspace.Interaction.Sink->CommitTransforms(edits);
    }
}

TEST_F(CarryTest, TheMoveGizmoCarriesTheCorridorLikeTheOperation)
{
    Drag(Workspace, Scene(), { Find("Hall") }, Vec3d{ -2, 0, 0 });
    EXPECT_NEAR(VolumeOf("Corridor").Min.X, 6.25f, 1e-3f);
    EXPECT_NEAR(VolumeOf("Hall").Min.X, -2.0f, 1e-3f);
    Commands.Undo();
    EXPECT_NEAR(VolumeOf("Corridor").Min.X, 8.25f, 1e-3f) << "the drag and its carry are one undo step";
    EXPECT_NEAR(VolumeOf("Hall").Min.X, 0.0f, 1e-3f) << "undo returns to where the drag began, not to its preview";
}

TEST_F(CarryTest, DraggingBothSpacesTogetherPullsNothing)
{
    Drag(Workspace, Scene(), { Find("Hall"), Find("Corridor") }, Vec3d{ -2, 0, 0 });
    EXPECT_NEAR(VolumeOf("Corridor").Min.X, 6.25f, 1e-3f);
    EXPECT_NEAR(VolumeOf("Corridor").Max.X, 12.0f, 1e-3f) << "moved, not stretched";
}

TEST_F(CarryTest, TheGizmoEditsTheLevelWhileAnAgentWritesADraft)
{
    Ok("draft.begin", R"({"label": "Elsewhere"})");
    Drag(Workspace, Scene(), { Find("Hall") }, Vec3d{ -2, 0, 0 });
    EXPECT_NEAR(VolumeOf("Hall").Min.X, -2.0f, 1e-3f);
    EXPECT_TRUE(Ok("draft.diff", "{}").Find("steps")->AsArray().empty()) << "the designer's drag is no step of the draft";
}

TEST_F(SpaceOperationsTest, TheSpaceToolMakesTheSpaceTheOperationWould)
{
    ToolRegistry& tools = *Workspace.Interaction.Tools;
    SpaceTool* tool = nullptr;
    for (const std::unique_ptr<ITool>& candidate : tools.GetTools())
        if (candidate->GetId() == "space")
            tool = static_cast<SpaceTool*>(candidate.get());
    ASSERT_NE(tool, nullptr) << "the Space tool is offered";

    tool->Create(tools.GetContext(), Vec3d{ 8, 0, 6 }, Vec3d{ 0, 0, 0 });
    const EntityId made = Find("Space");
    ASSERT_TRUE(made.IsValid());
    EXPECT_TRUE(Scene().GetRegistry().Components.HasComponent<Shell>(made));
    EXPECT_EQ(Walls(), 12u);
    const JsonValue described = Ok("entity.describe", R"({"subject": "@Space"})");
    EXPECT_NEAR(described.Find("entity")->Find("max")->AsArray()[1].AsNumber(), 3.0, 1e-3)
        << "the project's space height";
    EXPECT_EQ(Workspace.Selection.GetSelection().size(), 1u);

    tool->Create(tools.GetContext(), Vec3d{ 20, 0, 0 }, Vec3d{ 24, 0, 4 });
    EXPECT_TRUE(Find("Space 1").IsValid()) << "each new space takes the next free name";
    Commands.Undo();
    EXPECT_FALSE(Find("Space 1").IsValid());
    EXPECT_TRUE(Find("Space").IsValid());
}
