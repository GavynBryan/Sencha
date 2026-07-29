#include <gtest/gtest.h>

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/WorldDocument.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <core/logging/LoggingProvider.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

// Two-zone world: a brush entity in the focus zone, the second zone open as
// context, ready for a cross-zone move.
class MoveEntitiesToZoneTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_movezone_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root);

        World.NewWorld("TestWorld");
        SourceZone = World.Manifest().Zones[0].Id;
        TargetZone = World.AddZone(World.Manifest().Graphs[0].Id, "Target");
        ASSERT_TRUE(World.LoadZone(TargetZone));
        Entity = World.FocusDocument().GetScene().CreateBrush(Vec3d{ 5, 0, 0 });
        OriginalMeshId = World.FocusDocument().GetScene().TryGetBrush(Entity)->Id;
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    [[nodiscard]] std::string WorldPath() const { return (Root / "test.sworld").string(); }

    [[nodiscard]] EditorDocument& Source() { return World.FocusDocument(); }
    [[nodiscard]] EditorDocument& Target() { return *World.ZoneDocument(TargetZone); }

    // Builds and executes the move of Entity on the stack; fails the test if
    // the factory refuses.
    void ExecuteMove()
    {
        const EntityId entities[] = { Entity };
        auto command = MakeMoveEntitiesToZoneCommand(entities, World, TargetZone, Selection);
        ASSERT_NE(command, nullptr);
        Stack.Execute(std::move(command));
    }

    LoggingProvider Logging;   // sink-less: silent
    fs::path Root;
    WorldDocument World{ Logging };
    SelectionContext Context;
    SelectionService Selection{ Context };
    CommandStack Stack;
    ZoneId SourceZone;
    ZoneId TargetZone;
    EntityId Entity;
    BrushId OriginalMeshId;
};

} // namespace

TEST_F(MoveEntitiesToZoneTest, MoveRestoresEntityInTargetRegistry)
{
    ExecuteMove();

    EXPECT_EQ(Source().GetScene().GetEntityCount(), 0u);
    ASSERT_EQ(Target().GetScene().GetEntityCount(), 1u);
    const EntityId moved = Target().GetScene().GetAllEntities()[0];
    const Transform3f* transform = Target().GetScene().TryGetTransform(moved);
    ASSERT_NE(transform, nullptr);
    EXPECT_FLOAT_EQ(transform->Position.X, 5.0f);
}

TEST_F(MoveEntitiesToZoneTest, MoveCarriesBrushMeshSidecarEntry)
{
    const BrushMesh sourceMesh = *Source().GetScene().TryGetBrushMesh(Entity);

    ExecuteMove();

    const EntityId moved = Target().GetScene().GetAllEntities()[0];
    const BrushMesh* targetMesh = Target().GetScene().TryGetBrushMesh(moved);
    ASSERT_NE(targetMesh, nullptr);
    EXPECT_EQ(targetMesh->Vertices.size(), sourceMesh.Vertices.size());
    EXPECT_EQ(targetMesh->Faces.size(), sourceMesh.Faces.size());
    // The source store released its entry: the move is a transfer, not a copy.
    EXPECT_EQ(Source().GetScene().GetBrushMeshStore().Find(OriginalMeshId), nullptr);
}

TEST_F(MoveEntitiesToZoneTest, MoveUnsharesInstancedMesh)
{
    // A second placement sharing the same BrushId (restore of a live mesh id
    // joins the instance group instead of re-seating).
    const EntitySnapshot snapshot = Source().CaptureEntity(Entity);
    const EntityId sibling = Source().RestoreEntity(snapshot, /*freshMesh*/ false);
    ASSERT_EQ(Source().GetScene().TryGetBrush(sibling)->Id, OriginalMeshId);
    ASSERT_TRUE(Source().GetScene().IsBrushInstanced(Entity));

    ExecuteMove();

    // The stay-behind placement keeps the original mesh alive in the source.
    EXPECT_NE(Source().GetScene().GetBrushMeshStore().Find(OriginalMeshId), nullptr);
    // The moved copy has its own mesh under a fresh id in the target store.
    const EntityId moved = Target().GetScene().GetAllEntities()[0];
    const BrushComponent* movedBrush = Target().GetScene().TryGetBrush(moved);
    ASSERT_NE(movedBrush, nullptr);
    EXPECT_FALSE(Target().GetScene().IsBrushInstanced(moved));
    EXPECT_NE(Target().GetScene().TryGetBrushMesh(moved), nullptr);
}

TEST_F(MoveEntitiesToZoneTest, UndoReseatsOriginalBrushIdInSource)
{
    ExecuteMove();
    Stack.Undo();

    EXPECT_EQ(Target().GetScene().GetEntityCount(), 0u);
    ASSERT_EQ(Source().GetScene().GetEntityCount(), 1u);
    const EntityId restored = Source().GetScene().GetAllEntities()[0];
    ASSERT_NE(Source().GetScene().TryGetBrush(restored), nullptr);
    EXPECT_EQ(Source().GetScene().TryGetBrush(restored)->Id, OriginalMeshId);
    EXPECT_NE(Source().GetScene().GetBrushMeshStore().Find(OriginalMeshId), nullptr);
}

TEST_F(MoveEntitiesToZoneTest, RedoMovesAgainWithFreshTargetId)
{
    ExecuteMove();
    const BrushId firstTargetId =
        Target().GetScene().TryGetBrush(Target().GetScene().GetAllEntities()[0])->Id;

    Stack.Undo();
    Stack.Redo();

    EXPECT_EQ(Source().GetScene().GetEntityCount(), 0u);
    ASSERT_EQ(Target().GetScene().GetEntityCount(), 1u);
    const BrushId secondTargetId =
        Target().GetScene().TryGetBrush(Target().GetScene().GetAllEntities()[0])->Id;
    // The store never reuses a freed id, so the redo restore mints a new one.
    EXPECT_NE(secondTargetId, firstTargetId);
    EXPECT_NE(Target().GetScene().TryGetBrushMesh(Target().GetScene().GetAllEntities()[0]),
              nullptr);
}

TEST_F(MoveEntitiesToZoneTest, MoveMarksBothDocumentsDirty)
{
    ASSERT_TRUE(World.SaveWorldAs(WorldPath()));
    ASSERT_FALSE(Source().IsDirty());
    ASSERT_FALSE(Target().IsDirty());

    // The save rewrote nothing about entity identity; the captured id is live.
    ExecuteMove();

    EXPECT_TRUE(Source().IsDirty());
    EXPECT_TRUE(Target().IsDirty());
}

TEST_F(MoveEntitiesToZoneTest, FactoryRefusesUnloadedTarget)
{
    const ZoneId closed = World.AddZone(World.Manifest().Graphs[0].Id, "Closed");
    const EntityId entities[] = { Entity };
    EXPECT_EQ(MakeMoveEntitiesToZoneCommand(entities, World, closed, Selection), nullptr);
}

TEST_F(MoveEntitiesToZoneTest, FactoryRefusesFocusTarget)
{
    const EntityId entities[] = { Entity };
    EXPECT_EQ(MakeMoveEntitiesToZoneCommand(entities, World, SourceZone, Selection), nullptr);
}

TEST_F(MoveEntitiesToZoneTest, FactoryRefusesEmptySelection)
{
    EXPECT_EQ(MakeMoveEntitiesToZoneCommand({}, World, TargetZone, Selection), nullptr);
}

TEST_F(MoveEntitiesToZoneTest, UnloadZoneFiresObserver)
{
    ZoneId unloaded{};
    World.OnZoneUnloaded = [&](ZoneId zone) { unloaded = zone; };

    ASSERT_TRUE(World.UnloadZone(TargetZone));
    EXPECT_EQ(unloaded, TargetZone);
}

namespace
{

[[nodiscard]] size_t CountRecords(const WorldDocument& world, std::string_view ruleId)
{
    size_t count = 0;
    for (const ContentRiskRecord& record : world.ValidationRecords())
        if (record.RuleId == ruleId)
            ++count;
    return count;
}

} // namespace

TEST_F(MoveEntitiesToZoneTest, EntityOutsideBoundsFires)
{
    ASSERT_TRUE(World.SaveWorldAs(WorldPath()));

    ASSERT_TRUE(World.SetZoneBounds(SourceZone,
        Aabb3d::FromMinMax(Vec3d{ -1, -1, -1 }, Vec3d{ 1, 1, 1 })));
    ASSERT_TRUE(World.SaveWorld());

    EXPECT_EQ(CountRecords(World, "partition.zone.entity_outside_bounds"), 1u);
    for (const ContentRiskRecord& record : World.ValidationRecords())
        if (record.RuleId == "partition.zone.entity_outside_bounds")
        {
            EXPECT_EQ(record.Severity, ContentRiskSeverity::Warning);
            EXPECT_EQ(record.SourceId, SourceZone.Value);
        }
}

TEST_F(MoveEntitiesToZoneTest, EntityOutsideBoundsSilentWhenContained)
{
    World.SetZoneBounds(SourceZone,
        Aabb3d::FromMinMax(Vec3d{ -10, -10, -10 }, Vec3d{ 10, 10, 10 }));
    ASSERT_TRUE(World.SaveWorldAs(WorldPath()));
    EXPECT_EQ(CountRecords(World, "partition.zone.entity_outside_bounds"), 0u);
}

TEST_F(MoveEntitiesToZoneTest, EntityOutsideBoundsDoesNotRewriteOverrideOnSave)
{
    ASSERT_TRUE(World.SaveWorldAs(WorldPath()));
    const Aabb3d override = World.Manifest().Zones[0].Bounds;
    ASSERT_TRUE(World.SetZoneBounds(SourceZone, override));

    Transform3f moved = *Source().GetScene().TryGetTransform(Entity);
    moved.Position = Vec3d{ 100.0f, 0.0f, 0.0f };
    Source().GetScene().SetTransform(Entity, moved);
    Source().MarkDirty();
    ASSERT_TRUE(World.SaveWorld());

    EXPECT_EQ(CountRecords(World, "partition.zone.entity_outside_bounds"), 1u);
}

TEST_F(MoveEntitiesToZoneTest, UnloadClearsUndoStack)
{
    // The workspace binding: any zone unload drops the stack, because queued
    // commands may reference the destroyed document.
    World.OnZoneUnloaded = [this](ZoneId) { Stack.Clear(); };
    const ZoneId sibling = World.AddZone(World.Manifest().Graphs[0].Id, "Sibling");
    ASSERT_TRUE(World.LoadZone(sibling));

    ExecuteMove();
    ASSERT_TRUE(Stack.CanUndo());

    // The move's target is dirty and refuses to unload; the clean sibling goes.
    ASSERT_TRUE(World.UnloadZone(sibling));
    EXPECT_FALSE(Stack.CanUndo());
}
