// Brush instancing: duplicated-as-instance entities SHARE one sidecar mesh
// (same BrushId), so editing any instance edits them all, and destroying an
// instance frees the mesh only when the last user goes.

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/DocumentSerialization.h"
#include "document/commands/BreakInstanceCommand.h"
#include "document/commands/DuplicateEntitiesCommand.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <array>

namespace
{
    class BrushInstanceTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        EntityId Instantiate(EntityId source)
        {
            const std::array<EntityId, 1> sources = { source };
            const std::array<Transform3f, 1> transforms = { *Scene.TryGetTransform(source) };
            DuplicateEntitiesCommand command(sources, transforms, Scene, Document, Selection,
                                             /*asInstance*/ true);
            command.Execute();
            return Selection.GetPrimarySelection().Entity;
        }

        LoggingProvider  Logging;
        EditorDocument   Document{ Logging };
        EditorScene&     Scene = Document.GetScene();
        SelectionContext Context;
        SelectionService Selection{ Context };
    };

    TEST_F(BrushInstanceTest, InstanceSharesTheSourceMesh)
    {
        const EntityId source = Scene.CreateBrush({}, { 1.0f, 1.0f, 1.0f });
        const std::size_t meshCount = Scene.GetBrushMeshStore().Count();

        const EntityId instance = Instantiate(source);
        ASSERT_TRUE(Scene.HasEntity(instance));
        EXPECT_EQ(Scene.GetBrushMeshStore().Count(), meshCount); // no new mesh
        ASSERT_NE(Scene.TryGetBrush(instance), nullptr);
        EXPECT_TRUE(Scene.TryGetBrush(source)->Id == Scene.TryGetBrush(instance)->Id);

        // Editing either entity edits the one shared mesh.
        BrushMesh edited = *Scene.TryGetBrushMesh(instance);
        edited.Vertices.push_back(BrushVertex{ Vec3d{ 9.0f, 9.0f, 9.0f } });
        const std::size_t editedCount = edited.Vertices.size();
        Scene.SetBrushMesh(instance, std::move(edited));
        EXPECT_EQ(Scene.TryGetBrushMesh(source)->Vertices.size(), editedCount);
    }

    TEST_F(BrushInstanceTest, DestroyingOneInstanceKeepsTheSharedMesh)
    {
        const EntityId source = Scene.CreateBrush({}, { 1.0f, 1.0f, 1.0f });
        const BrushId shared = Scene.TryGetBrush(source)->Id;
        const EntityId instance = Instantiate(source);

        Scene.DestroyEntity(instance);
        EXPECT_NE(Scene.GetBrushMeshStore().Find(shared), nullptr);
        EXPECT_NE(Scene.TryGetBrushMesh(source), nullptr);

        // The last user frees it.
        Scene.DestroyEntity(source);
        EXPECT_EQ(Scene.GetBrushMeshStore().Find(shared), nullptr);
    }

    TEST_F(BrushInstanceTest, UndoOfInstanceDuplicationLeavesSourceIntact)
    {
        const EntityId source = Scene.CreateBrush({}, { 1.0f, 1.0f, 1.0f });
        const BrushId shared = Scene.TryGetBrush(source)->Id;

        const std::array<EntityId, 1> sources = { source };
        const std::array<Transform3f, 1> transforms = { *Scene.TryGetTransform(source) };
        DuplicateEntitiesCommand command(sources, transforms, Scene, Document, Selection, true);
        command.Execute();
        command.Undo();

        EXPECT_EQ(Scene.GetEntityCount(), 1u);
        EXPECT_NE(Scene.GetBrushMeshStore().Find(shared), nullptr);

        // Redo after an edit must not clobber the shared mesh with the stale
        // snapshot: instances always adopt the live mesh.
        BrushMesh edited = *Scene.TryGetBrushMesh(source);
        edited.Vertices.push_back(BrushVertex{ Vec3d{ 5.0f, 5.0f, 5.0f } });
        const std::size_t editedCount = edited.Vertices.size();
        Scene.SetBrushMesh(source, std::move(edited));
        command.Execute();
        EXPECT_EQ(Scene.TryGetBrushMesh(source)->Vertices.size(), editedCount);
    }

    TEST_F(BrushInstanceTest, MakeUniqueDivergesFromTheGroup)
    {
        const EntityId source = Scene.CreateBrush({}, { 1.0f, 1.0f, 1.0f });
        const EntityId instance = Instantiate(source);
        const BrushId sharedId = Scene.TryGetBrush(source)->Id;

        auto command = MakeBreakInstanceCommand(Scene, Document, instance);
        ASSERT_NE(command, nullptr);
        command->Execute();

        // Own mesh now: editing the unique brush no longer touches the source.
        ASSERT_NE(Scene.TryGetBrush(instance), nullptr);
        EXPECT_FALSE(Scene.TryGetBrush(instance)->Id == sharedId);
        BrushMesh edited = *Scene.TryGetBrushMesh(instance);
        edited.Vertices.push_back(BrushVertex{ Vec3d{ 7.0f, 7.0f, 7.0f } });
        const std::size_t editedCount = edited.Vertices.size();
        Scene.SetBrushMesh(instance, std::move(edited));
        EXPECT_EQ(Scene.TryGetBrushMesh(source)->Vertices.size(), editedCount - 1);

        command->Undo();
        EXPECT_TRUE(Scene.TryGetBrush(instance)->Id == sharedId);
    }

    TEST_F(BrushInstanceTest, MakeUniqueRejectsNonInstancedBrushes)
    {
        const EntityId lone = Scene.CreateBrush({}, { 1.0f, 1.0f, 1.0f });
        EXPECT_EQ(MakeBreakInstanceCommand(Scene, Document, lone), nullptr);
    }
}
