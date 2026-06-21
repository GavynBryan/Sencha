// DeleteEntityCommand: entity deletion must be undoable, restoring the entity's
// full state (every registered component plus the brush sidecar mesh), and a
// redo after an undo must delete the recreated entity (id retargeting). The
// capture is registry-driven, so it covers any component type; here it is
// exercised over the three the editor authors without an asset system (brush,
// plain transform-only, camera). The asset-backed StaticMeshComponent path needs
// a live AssetSystem and is covered at runtime, not here.

#include "level/LevelDocument.h"
#include "level/LevelScene.h"
#include "level/LevelSerialization.h"
#include "level/commands/DeleteEntityCommand.h"

#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    class DeleteEntityCommandTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterLevelSerializers(); }

        // A stable text form of an entity's captured component state, for
        // before/after comparison across a delete + undo round-trip.
        std::string Components(EntityId entity) const
        {
            return JsonStringify(Document.CaptureEntity(entity).Components, true);
        }

        LoggingProvider Logging;
        LevelDocument   Document{ Logging };
        LevelScene&     Scene = Document.GetScene();
    };

    TEST_F(DeleteEntityCommandTest, BrushRoundTripsAndFreesMesh)
    {
        const EntityId entity = Scene.CreateBrush(Vec3d{ 1.0, 2.0, 3.0 }, Vec3d{ 0.5, 0.5, 0.5 });
        const std::string before = Components(entity);
        const std::size_t meshCount = Scene.GetBrushMeshStore().Count();
        ASSERT_NE(Scene.TryGetBrushMesh(entity), nullptr);

        DeleteEntityCommand command(entity, Scene, Document);

        command.Execute();
        EXPECT_FALSE(Scene.HasEntity(entity));
        EXPECT_EQ(Scene.GetEntityCount(), 0u);
        EXPECT_EQ(Scene.GetBrushMeshStore().Count(), meshCount - 1); // mesh freed, not leaked

        command.Undo();
        ASSERT_EQ(Scene.GetEntityCount(), 1u);
        const EntityId restored = Scene.GetAllEntities()[0];
        EXPECT_NE(Scene.TryGetBrushMesh(restored), nullptr);
        EXPECT_EQ(Scene.GetBrushMeshStore().Count(), meshCount);
        EXPECT_EQ(Components(restored), before);

        // Redo deletes the recreated entity, not the stale original id.
        command.Execute();
        EXPECT_FALSE(Scene.HasEntity(restored));
        EXPECT_EQ(Scene.GetEntityCount(), 0u);
        EXPECT_EQ(Scene.GetBrushMeshStore().Count(), meshCount - 1);
    }

    TEST_F(DeleteEntityCommandTest, PlainEntityRoundTrips)
    {
        // A transform-only entity (the case the old command silently failed to
        // restore: its Undo returned early when no brush/camera was captured).
        const EntityId entity = Scene.CreateEntity(Vec3d{ 4.0, 5.0, 6.0 });
        const std::string before = Components(entity);

        DeleteEntityCommand command(entity, Scene, Document);

        command.Execute();
        EXPECT_EQ(Scene.GetEntityCount(), 0u);

        command.Undo();
        ASSERT_EQ(Scene.GetEntityCount(), 1u);
        EXPECT_EQ(Components(Scene.GetAllEntities()[0]), before);
    }

    TEST_F(DeleteEntityCommandTest, CameraRoundTrips)
    {
        const EntityId entity = Scene.CreateCamera(Vec3d{ 7.0, 8.0, 9.0 });
        const std::string before = Components(entity);

        DeleteEntityCommand command(entity, Scene, Document);

        command.Execute();
        EXPECT_EQ(Scene.GetEntityCount(), 0u);

        command.Undo();
        ASSERT_EQ(Scene.GetEntityCount(), 1u);
        EXPECT_EQ(Components(Scene.GetAllEntities()[0]), before);
    }
} // namespace
