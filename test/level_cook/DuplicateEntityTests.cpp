// EditorDocument::DuplicateEntity must produce a fully independent copy: a live
// duplicate of a brush gets its OWN sidecar mesh (a fresh BrushId), so editing
// one brush never changes the other. This is the regression guard for the
// "duplicates are instanced" bug. RestoreEntity's default (freshMesh == false)
// still re-seats at the original id for undo-of-delete; that path is covered by
// DeleteEntityCommandTests.

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/DocumentSerialization.h"
#include "document/EntityNameComponent.h"
#include "document/commands/DuplicateEntitiesCommand.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace
{
    class DuplicateEntityTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        LoggingProvider Logging;
        EditorDocument   Document{ Logging };
        EditorScene&     Scene = Document.GetScene();
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
        ASSERT_NE(Scene.TryGetWorldTransform(copy), nullptr);
        EXPECT_FLOAT_EQ(Scene.TryGetWorldTransform(copy)->Position.X, 1.0f);
    }

    // Committed duplicates claim fresh names once, into their snapshots, so
    // redo restores exactly what the first Execute minted.
    TEST_F(DuplicateEntityTest, ACommittedDuplicateClaimsTheNextFreeName)
    {
        SelectionContext context;
        SelectionService selection{ context };
        const EntityId brush = Scene.CreateBrush({}, { 1, 1, 1 });
        auto* named =
            Scene.GetRegistry().Components.TryGet<EntityNameComponent>(brush);
        ASSERT_NE(named, nullptr);
        named->Value = InlineString<64>("North Door");

        const std::array<EntityId, 1> sources = { brush };
        const std::array<Transform3f, 1> transforms = { Transform3f::Identity() };
        DuplicateEntitiesCommand command(sources, transforms, Scene, Document,
                                         selection, /*asInstance*/ false);
        command.Execute();

        const auto nameOf = [&](EntityId entity)
        {
            const auto* name = Document.GetRegistry()
                                   .Components.TryGet<EntityNameComponent>(entity);
            return name != nullptr ? std::string(name->Value.View()) : std::string();
        };
        const EntityId copy = selection.GetPrimarySelection().Entity;
        EXPECT_EQ(nameOf(copy), "North Door 1");

        // Redo restores the same minted name, not a re-allocation.
        command.Undo();
        command.Execute();
        EXPECT_EQ(nameOf(selection.GetPrimarySelection().Entity), "North Door 1");

        // A numeric suffix joins its family instead of nesting suffixes:
        // with only "Brush 7" alive, the bare base is the smallest free name.
        auto* renamed =
            Scene.GetRegistry().Components.TryGet<EntityNameComponent>(brush);
        renamed->Value = InlineString<64>("Brush 7");
        DuplicateEntitiesCommand again(sources, transforms, Scene, Document,
                                       selection, false);
        again.Execute();
        EXPECT_EQ(nameOf(selection.GetPrimarySelection().Entity), "Brush");
    }

    TEST_F(DuplicateEntityTest, AnUnnamedSourceDuplicatesUnnamed)
    {
        SelectionContext context;
        SelectionService selection{ context };
        const EntityId source = Scene.CreateEntity({});
        // Legacy content: strip the factory default.
        Scene.GetRegistry().Components.RemoveComponent<EntityNameComponent>(source);

        const std::array<EntityId, 1> sources = { source };
        const std::array<Transform3f, 1> transforms = { Transform3f::Identity() };
        DuplicateEntitiesCommand command(sources, transforms, Scene, Document,
                                         selection, false);
        command.Execute();
        const EntityId copy = selection.GetPrimarySelection().Entity;
        EXPECT_EQ(Document.GetRegistry().Components.TryGet<EntityNameComponent>(copy),
                  nullptr);
    }
} // namespace
