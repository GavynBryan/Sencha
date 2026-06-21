// LevelDocument::DuplicateEntity must produce a fully independent copy: a live
// duplicate of a brush gets its OWN sidecar mesh (a fresh BrushId), so editing
// one brush never changes the other. This is the regression guard for the
// "duplicates are instanced" bug. RestoreEntity's default (freshMesh == false)
// still re-seats at the original id for undo-of-delete; that path is covered by
// DeleteEntityCommandTests.

#include "level/LevelDocument.h"
#include "level/LevelScene.h"
#include "level/LevelSerialization.h"

#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

namespace
{
    class DuplicateEntityTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterLevelSerializers(); }

        LoggingProvider Logging;
        LevelDocument   Document{ Logging };
        LevelScene&     Scene = Document.GetScene();
    };

    TEST_F(DuplicateEntityTest, BrushCopyHasItsOwnMesh)
    {
        const EntityId source = Scene.CreateBrush(Vec3d{ 0.0, 0.0, 0.0 }, Vec3d{ 1.0, 1.0, 1.0 });
        const std::size_t meshCount = Scene.GetBrushMeshStore().Count();

        const EntityId copy = Document.DuplicateEntity(source);
        ASSERT_TRUE(Scene.HasEntity(copy));
        EXPECT_EQ(Scene.GetEntityCount(), 2u);
        EXPECT_EQ(Scene.GetBrushMeshStore().Count(), meshCount + 1); // a second, distinct mesh

        // Distinct brush ids => distinct sidecar meshes.
        ASSERT_NE(Scene.TryGetBrush(source), nullptr);
        ASSERT_NE(Scene.TryGetBrush(copy), nullptr);
        EXPECT_FALSE(Scene.TryGetBrush(source)->Id == Scene.TryGetBrush(copy)->Id);

        // Editing the copy's mesh must not touch the source's.
        const std::size_t sourceVerts = Scene.TryGetBrushMesh(source)->Vertices.size();
        BrushMesh edited = *Scene.TryGetBrushMesh(copy);
        edited.Vertices.push_back(BrushVertex{ Vec3d{ 9.0, 9.0, 9.0 } });
        Scene.SetBrushMesh(copy, edited);

        EXPECT_EQ(Scene.TryGetBrushMesh(source)->Vertices.size(), sourceVerts);
        EXPECT_EQ(Scene.TryGetBrushMesh(copy)->Vertices.size(), sourceVerts + 1);
    }

    TEST_F(DuplicateEntityTest, PlainEntityDuplicates)
    {
        const EntityId source = Scene.CreateEntity(Vec3d{ 1.0, 2.0, 3.0 });
        const EntityId copy = Document.DuplicateEntity(source);
        EXPECT_TRUE(Scene.HasEntity(copy));
        EXPECT_EQ(Scene.GetEntityCount(), 2u);
        ASSERT_NE(Scene.TryGetTransform(copy), nullptr);
        EXPECT_FLOAT_EQ(Scene.TryGetTransform(copy)->Position.X, 1.0f);
    }
} // namespace
