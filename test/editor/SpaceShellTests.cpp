// A space's shell, as wall brushes it builds: children of the space, the room
// side of each wall apart from its outside, cooked and cast against like any
// brush, rebuilt when what they were built from changes, and left alone once
// edited by hand.

#include "WorkspaceFixture.h"

#include "document/BrushComponents.h"
#include "document/BrushCookInput.h"
#include "document/EntityChangeSet.h"
#include "document/EntityNameComponent.h"
#include "document/SpaceComponents.h"
#include "document/SpaceWalls.h"
#include "document/commands/ValueCommand.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"
#include "render/BrushDrawSet.h"
#include "viewport/Picking.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

namespace
{
class SpaceShellTest : public WorkspaceTest
{
protected:
    EntityId ShelledSpace(Vec3d center, Vec3d half, float thickness = 0.25f, std::string_view name = "Hall")
    {
        const EntityId entity = AddBrush(center, half);
        World& world = Scene().GetRegistry().Components;
        world.AddComponent(entity, BrushVolume{});
        world.AddComponent(entity, Space{});
        world.AddComponent(entity, Shell{ .Thickness = thickness });
        world.TryGet<EntityNameComponent>(entity)->Value = InlineString<64>(std::string(name));
        Rebuild();
        return entity;
    }

    void Rebuild()
    {
        Scene().RefreshDerivedTransforms();
        EntityChangeRecorder recorder(Workspace.ActiveDocument());
        RebuildStaleSpaceWalls(Workspace.ActiveDocument(), recorder);
    }

    Aabb3d CookedBounds()
    {
        Aabb3d bounds = Aabb3d::Empty();
        for (const CookBrushGeometry& brush : CollectCookBrushes(Scene(), Default))
            bounds.ExpandToInclude(brush.WorldBounds);
        return bounds;
    }

    std::vector<std::string> WallNames(EntityId space)
    {
        std::vector<std::string> names;
        for (const EntityId wall : SpaceWalls(Scene(), space))
            names.emplace_back(Scene().GetRegistry().Components.TryGet<EntityNameComponent>(wall)->Value.View());
        std::ranges::sort(names);
        return names;
    }

    EntityId Named(std::string_view name)
    {
        for (const EntityId entity : Scene().GetAllEntities())
            if (const auto* label = Scene().GetRegistry().Components.TryGet<EntityNameComponent>(entity);
                label != nullptr && label->Value.View() == name)
                return entity;
        return {};
    }

    const AssetRef Default{ AssetType::Material, "asset://materials/dev/gray.smat" };
};

void ExpectNear(const Vec3d& actual, const Vec3d& expected)
{
    EXPECT_NEAR(actual.X, expected.X, 1e-3f);
    EXPECT_NEAR(actual.Y, expected.Y, 1e-3f);
    EXPECT_NEAR(actual.Z, expected.Z, 1e-3f);
}
}

TEST_F(SpaceShellTest, AShelledSpaceCooksItsWallsAndNotItsAir)
{
    (void)ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    EXPECT_EQ(CollectCookBrushes(Scene(), Default).size(), 12u) << "six room sides and six outsides";
    const Aabb3d bounds = CookedBounds();
    ExpectNear(bounds.Min, { -4.25f, -0.25f, -3.25f });
    ExpectNear(bounds.Max, { 4.25f, 3.25f, 3.25f });
}

TEST_F(SpaceShellTest, EachWallIsABrushUnderItsSpaceNamedForTheSideItShows)
{
    const EntityId hall = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const std::vector<std::string> names = WallNames(hall);
    ASSERT_EQ(names.size(), 12u);
    EXPECT_EQ(names.front(), "Hall ceiling");
    EXPECT_NE(std::ranges::find(names, "Hall north wall"), names.end());
    EXPECT_NE(std::ranges::find(names, "Hall north wall outside"), names.end());
    std::set<std::uint32_t> slots;
    for (const EntityId wall : SpaceWalls(Scene(), hall))
    {
        EXPECT_EQ(Scene().GetParent(wall), hall);
        slots.insert(Scene().GetRegistry().Components.TryGet<GeneratedWall>(wall)->Slot);
    }
    EXPECT_EQ(slots.size(), 12u) << "every wall has its own slot";
    EXPECT_FALSE(SpaceWallsStale(Scene(), hall));
}

TEST_F(SpaceShellTest, ASpaceWithoutAShellBuildsNothing)
{
    const EntityId room = AddBrush({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    Scene().GetRegistry().Components.AddComponent(room, BrushVolume{});
    Scene().GetRegistry().Components.AddComponent(room, Space{});
    Rebuild();
    EXPECT_TRUE(CollectCookBrushes(Scene(), Default).empty());
    EXPECT_FALSE(SpaceWallsStale(Scene(), room));
}

TEST_F(SpaceShellTest, AFacePaintedOpenGetsNoWall)
{
    const AssetRef open{ AssetType::Material, "asset://materials/dev/open.smat" };
    Scene().SetOpenFaceMaterial(open);
    const EntityId courtyard = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    BrushMesh mesh = *Scene().TryGetBrushMesh(courtyard);
    for (BrushFace& face : mesh.Faces)
        if (face.Normal.Y > 0.99f)
            face.Material.Material = open;
    Scene().SetBrushMesh(courtyard, mesh);
    Rebuild();
    EXPECT_EQ(CollectCookBrushes(Scene(), Default).size(), 10u);
    EXPECT_NEAR(CookedBounds().Max.Y, 3.0f, 1e-3f) << "the walls stop on the open top";
}

TEST_F(SpaceShellTest, ARayFromInsideTheRoomLandsOnItsFloor)
{
    (void)ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const std::optional<SurfaceHit> hit =
        CastRayAgainstBrushes(Scene(), Ray3d(Vec3d{ 0, 1.5f, 0 }, Vec3d{ 0, -1, 0 }), 100.0f, false);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Point.Y, 0.0f, 1e-4f);
    EXPECT_GT(hit->Normal.Y, 0.99f);
    EXPECT_EQ(hit->Entity, Named("Hall floor"));
}

TEST_F(SpaceShellTest, MovingOrThickeningTheSpaceRewritesTheSameWalls)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const std::vector<EntityId> walls = SpaceWalls(Scene(), room);

    Scene().GetRegistry().Components.TryGet<LocalTransform>(room)->Value.Position.X = 10.0f;
    Scene().RefreshDerivedTransforms();
    EXPECT_TRUE(SpaceWallsStale(Scene(), room));
    Rebuild();
    ExpectNear(CookedBounds().Min, { 5.75f, -0.25f, -3.25f });

    Scene().GetRegistry().Components.TryGet<Shell>(room)->Thickness = 0.5f;
    Rebuild();
    ExpectNear(CookedBounds().Min, { 5.5f, -0.5f, -3.5f });
    EXPECT_EQ(SpaceWalls(Scene(), room), walls) << "a rebuild rewrites each slot's brush, never mints new ones";
}

TEST_F(SpaceShellTest, WallsAreAsThickInTheWorldWhateverTheSpacesScale)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 2, 1.5f, 3 });
    Scene().GetRegistry().Components.TryGet<LocalTransform>(room)->Value.Scale = Vec3d{ 2, 1, 1 };
    Rebuild();
    const Aabb3d bounds = CookedBounds();
    ExpectNear(bounds.Min, { -4.25f, -0.25f, -3.25f });
    ExpectNear(bounds.Max, { 4.25f, 3.25f, 3.25f });
}

TEST_F(SpaceShellTest, AWallEditedByHandIsKeptThroughARebuild)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const EntityId north = Named("Hall north wall");
    BrushMesh bevelled = *Scene().TryGetBrushMesh(north);
    bevelled.Vertices.front().Position.Y += 0.2f;
    Scene().SetBrushMesh(north, bevelled);

    Scene().GetRegistry().Components.TryGet<Shell>(room)->Thickness = 0.5f;
    Rebuild();
    const GeneratedWall& kept = *Scene().GetRegistry().Components.TryGet<GeneratedWall>(north);
    EXPECT_EQ(kept.State, WallState::Kept);
    EXPECT_EQ(BrushMeshSignature(*Scene().TryGetBrushMesh(north)), BrushMeshSignature(bevelled));
    EXPECT_EQ(Scene().GetRegistry().Components.TryGet<GeneratedWall>(Named("Hall south wall"))->State,
              WallState::Generated);
    EXPECT_FALSE(SpaceWallsStale(Scene(), room)) << "a kept wall does not keep its space stale";

    EntityChangeRecorder recorder(Workspace.ActiveDocument());
    RebuildSpaceWalls(Workspace.ActiveDocument(), recorder, room, true);
    EXPECT_EQ(Scene().GetRegistry().Components.TryGet<GeneratedWall>(north)->State, WallState::Generated)
        << "reclaiming takes it back";
}

TEST_F(SpaceShellTest, ARepaintedWallKeepsItsPaintThroughARebuild)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const AssetRef brick{ AssetType::Material, "asset://materials/dev/brick.smat" };
    const EntityId north = Named("Hall north wall");
    BrushMesh painted = *Scene().TryGetBrushMesh(north);
    for (BrushFace& face : painted.Faces)
        face.Material.Material = brick;
    Scene().SetBrushMesh(north, painted);

    Scene().GetRegistry().Components.TryGet<Shell>(room)->Thickness = 0.5f;
    Rebuild();
    EXPECT_EQ(Scene().GetRegistry().Components.TryGet<GeneratedWall>(north)->State, WallState::Generated)
        << "paint is not a hand edit";
    for (const BrushFace& face : Scene().TryGetBrushMesh(north)->Faces)
        EXPECT_EQ(face.Material.Material.Path, brick.Path);
}

TEST_F(SpaceShellTest, ARefusedShellSaysWhyAndTakesItsWallsAway)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 }, 0.25f);
    BrushMesh bent = *Scene().TryGetBrushMesh(room);
    bent.Vertices.front().Position.Y += 0.4f;
    Scene().SetBrushMesh(room, bent);
    Rebuild();
    EXPECT_TRUE(CollectCookBrushes(Scene(), Default).empty());
    const std::optional<SpaceShell> shell = BuildSpaceShell(Scene(), room);
    ASSERT_TRUE(shell.has_value());
    ASSERT_FALSE(shell->Result.Problems.empty());
    EXPECT_EQ(shell->Result.Problems.front().Kind, ShellProblemKind::NonPlanarFace);
    EXPECT_TRUE(Scene().GetRegistry().Components.HasComponent<Space>(room));
}

TEST_F(SpaceShellTest, TheDrawSetHoldsTheAirApartFromTheWalls)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    BrushBakeCache bakes(BrushBakeCache::Gpu{
        .Bake = [next = 1u](const MeshGeometry&) mutable { return StaticMeshHandle{ next++, 1 }; },
        .Destroy = [](StaticMeshHandle) {},
        .Lease = [](const AssetRef&) { return MaterialHandle{ 1, 1 }; },
        .Release = [](MaterialHandle) {},
    });
    BrushDrawSet draws;
    ASSERT_TRUE(draws.Refresh(Scene(), bakes, Default));
    ASSERT_EQ(draws.Find(room)->Meshes.size(), 1u);
    EXPECT_EQ(draws.Find(room)->Meshes[0].Role, BrushRole::Volume);
    for (const EntityId wall : SpaceWalls(Scene(), room))
        for (const BrushDrawMesh& mesh : draws.Find(wall)->Meshes)
            EXPECT_EQ(mesh.Role, BrushRole::Solid);
}

TEST_F(SpaceShellTest, RoomsAWallApartBuildTheirSharedWallOnce)
{
    const EntityId west = ShelledSpace({ -2, 1.5f, 0 }, { 2, 1.5f, 3 }, 0.25f, "West");
    const EntityId east = ShelledSpace({ 2.25f, 1.5f, 0 }, { 2, 1.5f, 3 }, 0.25f, "East");
    Rebuild();
    EXPECT_EQ(CollectCookBrushes(Scene(), Default).size(), 23u)
        << "eleven walls, each a room side and an outside, and the shared one a side for each room";
    const std::vector<std::string> both = [&] {
        std::vector<std::string> names = WallNames(west);
        const std::vector<std::string> more = WallNames(east);
        names.insert(names.end(), more.begin(), more.end());
        return names;
    }();
    EXPECT_NE(std::ranges::find(both, "West east wall"), both.end());
    EXPECT_NE(std::ranges::find(both, "East west wall"), both.end()) << "each side of the shared wall is named for its room";

    Scene().GetRegistry().Components.TryGet<LocalTransform>(east)->Value.Position.X = 6.0f;
    Rebuild();
    EXPECT_EQ(CollectCookBrushes(Scene(), Default).size(), 24u) << "moving one away gives each its own wall back";
}

TEST_F(SpaceShellTest, ADoorEntityCutsTheWallItStandsInAndMovesItsHoleWithIt)
{
    (void)ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const EntityId door = Scene().CreateEntity(Vec3d{ 0, 0, -3.125f });
    Scene().GetRegistry().Components.AddComponent(door, Opening{});
    Rebuild();

    const auto northward = [this](float x) {
        return CastRayAgainstBrushes(Scene(), Ray3d(Vec3d{ x, 1, 0 }, Vec3d{ 0, 0, -1 }), 20.0f, false);
    };
    EXPECT_FALSE(northward(0.0f).has_value()) << "through the doorway";
    EXPECT_TRUE(northward(2.0f).has_value()) << "into the wall beside it";

    Scene().GetRegistry().Components.TryGet<LocalTransform>(door)->Value.Position.X = 2.0f;
    Rebuild();
    EXPECT_TRUE(northward(0.0f).has_value());
    EXPECT_FALSE(northward(2.0f).has_value());
}

TEST_F(SpaceShellTest, AnEditorCommandOnTheSpaceRebuildsItsWallsInTheSameUndoStep)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const auto northWallZ = [this] {
        Scene().RefreshDerivedTransforms();
        return Scene().EvaluatedWorldBounds(Named("Hall north wall"))->Max.Z;
    };
    const float before = northWallZ();
    const BrushMesh mesh = *Scene().TryGetBrushMesh(room);
    BrushMesh pushed = mesh;
    for (BrushVertex& vertex : pushed.Vertices)
        if (vertex.Position.Z < 0.0f)
            vertex.Position.Z -= 2.0f;
    Commands.Execute(MakeEditBrushMeshCommand(room, mesh, pushed, Scene(), Workspace.ActiveDocument()));
    EXPECT_FALSE(SpaceWallsStale(Scene(), room));
    EXPECT_NEAR(northWallZ(), before - 2.0f, 1e-3f);

    Commands.Undo();
    EXPECT_FALSE(Commands.CanUndo()) << "the walls rode in the face edit's step";
    EXPECT_NEAR(northWallZ(), before, 1e-3f);
    Commands.Redo();
    EXPECT_NEAR(northWallZ(), before - 2.0f, 1e-3f);
}

TEST_F(SpaceShellTest, SavedWallsLoadAsBuilt)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const EntityId door = Scene().CreateEntity(Vec3d{ 0, 0, -3.125f });
    Scene().GetRegistry().Components.AddComponent(door, Opening{ .Shape = CarveShape::Arch });
    Rebuild();
    LoggingProvider logging;
    EditorDocument reloaded(logging);
    std::string error;
    ASSERT_TRUE(reloaded.LoadFromSceneText(Workspace.ActiveDocument().ToSceneText(), &error)) << error;
    reloaded.GetScene().RefreshDerivedTransforms();
    const SubjectResolution space = ResolveEntitySubject(reloaded, PersistentIdText(Workspace.ActiveDocument(), room));
    ASSERT_TRUE(space.Resolved());
    EXPECT_FALSE(SpaceWallsStale(reloaded.GetScene(), space.Entities.front()));
    EntityChangeRecorder recorder(reloaded);
    RebuildSpaceWalls(reloaded, recorder, space.Entities.front());
    EXPECT_TRUE(recorder.Finish().Empty()) << "nothing reads as edited by hand";
}

namespace
{
class ShellDetachTest : public SpaceShellTest
{
protected:
    void SetUp() override
    {
        SpaceShellTest::SetUp();
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Level, Operations), errors));
    }

    JsonValue Detach(EntityId space)
    {
        return Operations.Invoke("shell.detach",
                                 *JsonParse(R"({"subject": ")" + PersistentIdText(Workspace.ActiveDocument(), space) + R"("})"));
    }

    std::vector<EntityId> ChildrenOf(EntityId parent)
    {
        std::vector<EntityId> children;
        for (const EntityId entity : Scene().GetAllEntities())
            if (const Parent* link = Scene().GetRegistry().Components.TryGet<Parent>(entity); link && link->Entity == parent)
                children.push_back(entity);
        return children;
    }

    LevelTarget Level{ Workspace.World, Commands };
    EditorOperations Operations;
};
}

TEST_F(ShellDetachTest, DetachedWallsStayAsOrdinaryBrushesUnderTheSpace)
{
    const EntityId hall = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const Aabb3d before = CookedBounds();
    const JsonValue answer = Detach(hall);
    ASSERT_EQ(answer.Find("status")->AsString(), "ok") << JsonStringify(answer);
    EXPECT_TRUE(answer.Find("data")->Find("created")->AsArray().empty());
    EXPECT_FALSE(Scene().GetRegistry().Components.HasComponent<Shell>(hall));
    EXPECT_TRUE(Scene().GetRegistry().Components.HasComponent<Space>(hall));
    EXPECT_EQ(ChildrenOf(hall).size(), 12u);
    EXPECT_TRUE(SpaceWalls(Scene(), hall).empty()) << "the space no longer manages them";

    Scene().RefreshDerivedTransforms();
    EXPECT_EQ(CollectCookBrushes(Scene(), Default).size(), 12u);
    const Aabb3d after = CookedBounds();
    ExpectNear(after.Min, before.Min);
    ExpectNear(after.Max, before.Max);
}

TEST_F(ShellDetachTest, UndoBringsTheShellBack)
{
    const EntityId hall = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    ASSERT_EQ(Detach(hall).Find("status")->AsString(), "ok");
    Commands.Undo();
    EXPECT_TRUE(Scene().GetRegistry().Components.HasComponent<Shell>(hall));
    EXPECT_EQ(SpaceWalls(Scene(), hall).size(), 12u);
    EXPECT_FALSE(SpaceWallsStale(Scene(), hall));
}

TEST_F(ShellDetachTest, ADetachedWallKeepsItsDoorway)
{
    const EntityId hall = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const EntityId door = Scene().CreateEntity(Vec3d{ 0, 0, -3.125f });
    Scene().GetRegistry().Components.AddComponent(door, Opening{});
    Rebuild();
    ASSERT_EQ(Detach(hall).Find("status")->AsString(), "ok");
    Scene().RefreshDerivedTransforms();
    EXPECT_FALSE(CastRayAgainstBrushes(Scene(), Ray3d(Vec3d{ 0, 1, 0 }, Vec3d{ 0, 0, -1 }), 20.0f, false).has_value());
}

TEST_F(ShellDetachTest, ASpaceWithoutAShellIsRefused)
{
    const EntityId hall = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    Scene().GetRegistry().Components.RemoveComponent<Shell>(hall);
    EXPECT_EQ(Detach(hall).Find("code")->AsString(), "shell.none");
    EXPECT_FALSE(Commands.CanUndo());
}

namespace
{
class ShellOperationsTest : public ShellDetachTest
{
protected:
    JsonValue Call(std::string_view name, std::string_view arguments)
    {
        return Operations.Invoke(name, *JsonParse(arguments));
    }

    std::vector<std::string> Rules()
    {
        const JsonValue answer = Call("level.diagnostics", "{}");
        std::vector<std::string> rules;
        for (const JsonValue& record : answer.Find("data")->Find("records")->AsArray())
            rules.push_back(record.Find("rule")->AsString());
        return rules;
    }

    static bool Has(const std::vector<std::string>& rules, std::string_view rule)
    {
        return std::ranges::find(rules, rule) != rules.end();
    }
};
}

TEST_F(ShellOperationsTest, AKeptWallTheSpaceMovedOnFromIsReportedAndRebuildTakesItBack)
{
    ASSERT_EQ(Call("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})").Find("status")->AsString(), "ok");
    const EntityId north = Named("Hall north wall");
    BrushMesh bevelled = *Scene().TryGetBrushMesh(north);
    bevelled.Vertices.front().Position.Y += 0.2f;
    Commands.Execute(MakeEditBrushMeshCommand(north, *Scene().TryGetBrushMesh(north), bevelled, Scene(),
                                              Workspace.ActiveDocument()));
    EXPECT_FALSE(Has(Rules(), "wall.kept_stale")) << "kept, but the space has not changed under it";

    ASSERT_EQ(Call("space.resize_face", R"({"subject": "@Hall", "face": "east", "distance": 1})").Find("status")->AsString(), "ok");
    EXPECT_EQ(Scene().GetRegistry().Components.TryGet<GeneratedWall>(north)->State, WallState::Kept);
    EXPECT_TRUE(Has(Rules(), "wall.kept_stale"));

    const JsonValue rebuilt = Call("shell.rebuild", R"({"subject": "@Hall"})");
    ASSERT_EQ(rebuilt.Find("status")->AsString(), "ok") << JsonStringify(rebuilt);
    EXPECT_EQ(Scene().GetRegistry().Components.TryGet<GeneratedWall>(north)->State, WallState::Generated);
    EXPECT_FALSE(Has(Rules(), "wall.kept_stale"));
    Commands.Undo();
    EXPECT_EQ(Scene().GetRegistry().Components.TryGet<GeneratedWall>(north)->State, WallState::Kept);
    EXPECT_EQ(Call("shell.rebuild", R"({"subject": "@Hall north wall"})").Find("code")->AsString(), "shell.none");
}

TEST_F(ShellOperationsTest, ASpaceWhoseWallsWereNeverBuiltIsStaleUntilTheNextEdit)
{
    const EntityId hall = AddBrush({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    World& components = Scene().GetRegistry().Components;
    components.AddComponent(hall, BrushVolume{});
    components.AddComponent(hall, Space{});
    components.AddComponent(hall, Shell{});
    Scene().RefreshDerivedTransforms();
    EXPECT_TRUE(Has(Rules(), "shell.stale"));
    ASSERT_EQ(Call("note.add", R"({"text": "anything", "at": [20, 0, 0]})").Find("status")->AsString(), "ok");
    EXPECT_FALSE(Has(Rules(), "shell.stale"));
    EXPECT_EQ(SpaceWalls(Scene(), hall).size(), 12u);
}
