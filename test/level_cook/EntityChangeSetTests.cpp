// Entity change sets: edits made directly to a document, recorded by persistent
// id, applied back and forth exactly, and refused over someone else's edit.

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityChangeSet.h"
#include "document/EntityNameComponent.h"
#include "document/commands/ApplyChangeSetCommand.h"

#include <core/logging/LoggingProvider.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
    class EntityChangeSetTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        EditorScene& Scene() { return Document.GetScene(); }

        PersistentEntityId Id(EntityId entity)
        {
            return Document.GetRegistry().Components.TryGet<PersistentIdComponent>(entity)->Id;
        }

        EntityId Find(PersistentEntityId id)
        {
            return Document.GetRegistry().Components.TryGetResource<PersistentEntityIndex>()->TryResolve(id);
        }

        std::string Name(EntityId entity)
        {
            const auto* name = Document.GetRegistry().Components.TryGet<EntityNameComponent>(entity);
            return name != nullptr ? std::string(name->Value.View()) : std::string{};
        }

        void Rename(EntityId entity, std::string_view name)
        {
            Scene().SetComponent(entity, EntityNameComponent{ InlineString<64>(name) });
        }

        LoggingProvider Logging;
        EditorDocument Document{ Logging };
    };

    TEST_F(EntityChangeSetTest, AnOverwriteKeepsTheEntityAndItsChildren)
    {
        const EntityId room = Scene().CreateBrush(Vec3d{ 0, 0, 0 });
        const EntityId prop = Scene().CreateEntity(Vec3d{ 1, 0, 0 });
        ASSERT_TRUE(Scene().SetParent(prop, room));
        Rename(room, "Hall");

        EntityChangeRecorder recorder(Document);
        recorder.Touch(room);
        Rename(room, "Atrium");
        const EntityChangeSet changes = recorder.Finish();
        ASSERT_EQ(changes.Changes.size(), 1u);

        ApplyEntityChangeSet(Document, changes, ChangeSetSide::Before);
        EXPECT_EQ(Name(room), "Hall");
        EXPECT_EQ(Find(Id(room)), room) << "the entity is overwritten in place, not replaced";
        ASSERT_NE(Document.GetRegistry().Components.TryGet<Parent>(prop), nullptr);
        EXPECT_EQ(Document.GetRegistry().Components.TryGet<Parent>(prop)->Entity, room);

        ApplyEntityChangeSet(Document, changes, ChangeSetSide::After);
        EXPECT_EQ(Name(room), "Atrium");
    }

    TEST_F(EntityChangeSetTest, CreationsAndDeletionsGoBackAndForthWithTheirMeshes)
    {
        const EntityId doomed = Scene().CreateBrush(Vec3d{ 5, 0, 0 }, Vec3d{ 2, 1, 2 });
        const PersistentEntityId doomedId = Id(doomed);
        const std::uint64_t doomedDigest = DigestOf(Document.CaptureEntity(doomed));

        EntityChangeRecorder recorder(Document);
        recorder.Touch(doomed);
        Scene().DestroyEntity(doomed);
        const EntityId made = Scene().CreateBrush(Vec3d{ -5, 0, 0 });
        recorder.Created(made);
        const PersistentEntityId madeId = Id(made);
        const EntityChangeSet changes = recorder.Finish();
        ASSERT_EQ(changes.Changes.size(), 2u);

        ApplyEntityChangeSet(Document, changes, ChangeSetSide::Before);
        EXPECT_FALSE(Find(madeId).IsValid());
        ASSERT_TRUE(Find(doomedId).IsValid());
        EXPECT_EQ(DigestOf(Document.CaptureEntity(Find(doomedId))), doomedDigest);

        ApplyEntityChangeSet(Document, changes, ChangeSetSide::After);
        EXPECT_FALSE(Find(doomedId).IsValid());
        ASSERT_TRUE(Find(madeId).IsValid());
        EXPECT_NE(Scene().TryGetBrush(Find(madeId)), nullptr);
    }

    TEST_F(EntityChangeSetTest, AnEditMadeSinceRecordingIsAConflict)
    {
        const EntityId room = Scene().CreateBrush(Vec3d{ 0, 0, 0 });
        const EntityId other = Scene().CreateBrush(Vec3d{ 9, 0, 0 });
        EntityChangeRecorder recorder(Document);
        recorder.Touch(room);
        Rename(room, "Atrium");
        const EntityChangeSet changes = recorder.Finish();

        // Recorded against the state before: the room has moved on since.
        EXPECT_EQ(ChangeSetConflicts(Document, changes).size(), 1u);
        ApplyEntityChangeSet(Document, changes, ChangeSetSide::Before);
        EXPECT_TRUE(ChangeSetConflicts(Document, changes).empty());

        // An edit to an entity the change set does not name is no conflict.
        Rename(other, "Elsewhere");
        EXPECT_TRUE(ChangeSetConflicts(Document, changes).empty());
        Rename(room, "Someone else's name");
        EXPECT_EQ(ChangeSetConflicts(Document, changes).size(), 1u);
    }

    TEST_F(EntityChangeSetTest, AnAdoptedCommandUndoesAndRedoesWhatWasAlreadyDone)
    {
        const EntityId room = Scene().CreateBrush(Vec3d{ 0, 0, 0 });
        Rename(room, "Hall");
        EntityChangeRecorder recorder(Document);
        recorder.Touch(room);
        Rename(room, "Atrium");

        CommandStack commands;
        commands.Execute(std::make_unique<ApplyChangeSetCommand>(Document, recorder.Finish(),
                                                                 ChangeSetSide::After));
        EXPECT_EQ(Name(room), "Atrium");
        commands.Undo();
        EXPECT_EQ(Name(room), "Hall");
        commands.Redo();
        EXPECT_EQ(Name(room), "Atrium");
    }
    TEST_F(EntityChangeSetTest, AnEntityTouchedButLeftAsItWasIsNoChange)
    {
        const EntityId room = Scene().CreateBrush(Vec3d{ 0, 0, 0 });
        Rename(room, "Hall");
        EntityChangeRecorder recorder(Document);
        recorder.Touch(room);
        Rename(room, "Hall");
        EXPECT_TRUE(recorder.Finish().Empty());
    }
}
