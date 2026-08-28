// What the document file carries besides components: authored names, per-entity
// view flags, and parentage. Each is captured by saving to JSON and loading
// into a fresh document, because that is the trip a designer's work actually
// takes; matching across the trip goes through persistent ids, since entity
// handles do not survive it.

#include "document/DocumentCook.h"
#include "document/DocumentCookSnapshot.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityNameComponent.h"
#include "document/commands/RenameEntityCommand.h"

#include <core/logging/LoggingProvider.h>
#include <world/identity/PersistentIdComponent.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace
{
    class DocumentPersistenceTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        [[nodiscard]] static PersistentEntityId IdOf(const EditorDocument& doc,
                                                     EntityId entity)
        {
            const auto* id =
                doc.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
            return id != nullptr ? id->Id : PersistentEntityId{};
        }

        [[nodiscard]] static EntityId FindById(const EditorDocument& doc,
                                               PersistentEntityId id)
        {
            for (EntityId entity : doc.GetScene().GetAllEntities())
                if (IdOf(doc, entity) == id)
                    return entity;
            return {};
        }

        [[nodiscard]] static std::string NameOf(const EditorDocument& doc,
                                                EntityId entity)
        {
            const auto* name =
                doc.GetRegistry().Components.TryGet<EntityNameComponent>(entity);
            return name != nullptr ? std::string(name->Value.View()) : std::string();
        }

        LoggingProvider Logging;
    };

    TEST_F(DocumentPersistenceTest, NameRoundTripsThroughTheDocumentFile)
    {
        EditorDocument source(Logging);
        const EntityId entity = source.GetScene().CreateEntity(Vec3d{});
        auto rename = MakeRenameEntityCommand(entity, "North Door",
                                              source.GetScene(), source);
        ASSERT_NE(rename, nullptr);
        rename->Execute();
        const PersistentEntityId id = IdOf(source, entity);

        EditorDocument loaded(Logging);
        ASSERT_TRUE(loaded.LoadFromJson(source.ToJson()));
        const EntityId restored = FindById(loaded, id);
        ASSERT_TRUE(restored.IsValid());
        EXPECT_EQ(NameOf(loaded, restored), "North Door");
    }

    TEST_F(DocumentPersistenceTest, RenameUndoesExactlyAndClearingRemovesTheComponent)
    {
        EditorDocument doc(Logging);
        EditorScene& scene = doc.GetScene();
        const EntityId entity = scene.CreateEntity(Vec3d{});

        auto first = MakeRenameEntityCommand(entity, "Lantern", scene, doc);
        ASSERT_NE(first, nullptr);
        first->Execute();
        EXPECT_EQ(NameOf(doc, entity), "Lantern");

        auto second = MakeRenameEntityCommand(entity, "Brazier", scene, doc);
        ASSERT_NE(second, nullptr);
        second->Execute();
        EXPECT_EQ(NameOf(doc, entity), "Brazier");
        second->Undo();
        EXPECT_EQ(NameOf(doc, entity), "Lantern");

        // Whitespace-only clears: the component exists only while a real name does.
        auto clear = MakeRenameEntityCommand(entity, "   ", scene, doc);
        ASSERT_NE(clear, nullptr);
        clear->Execute();
        EXPECT_EQ(doc.GetRegistry().Components.TryGet<EntityNameComponent>(entity),
                  nullptr);
        clear->Undo();
        EXPECT_EQ(NameOf(doc, entity), "Lantern");

        // Renaming to the current name is a no-op, not an undo entry.
        EXPECT_EQ(MakeRenameEntityCommand(entity, "Lantern", scene, doc), nullptr);
    }

    TEST_F(DocumentPersistenceTest, ViewFlagsRoundTripThroughTheDocumentFile)
    {
        EditorDocument source(Logging);
        EditorScene& scene = source.GetScene();
        const EntityId hidden = scene.CreateEntity(Vec3d{});
        const EntityId locked = scene.CreateEntity(Vec3d{});
        const EntityId plain = scene.CreateEntity(Vec3d{});
        scene.SetEntityVisible(hidden, false);
        scene.SetEntityLocked(locked, true);
        const PersistentEntityId hiddenId = IdOf(source, hidden);
        const PersistentEntityId lockedId = IdOf(source, locked);
        const PersistentEntityId plainId = IdOf(source, plain);

        EditorDocument loaded(Logging);
        ASSERT_TRUE(loaded.LoadFromJson(source.ToJson()));
        const EditorScene& loadedScene = loaded.GetScene();
        EXPECT_FALSE(loadedScene.IsEntityVisible(FindById(loaded, hiddenId)));
        EXPECT_FALSE(loadedScene.IsEntityLocked(FindById(loaded, hiddenId)));
        EXPECT_TRUE(loadedScene.IsEntityVisible(FindById(loaded, lockedId)));
        EXPECT_TRUE(loadedScene.IsEntityLocked(FindById(loaded, lockedId)));
        EXPECT_TRUE(loadedScene.IsEntityVisible(FindById(loaded, plainId)));
        EXPECT_FALSE(loadedScene.IsEntityLocked(FindById(loaded, plainId)));
    }

    TEST_F(DocumentPersistenceTest, HierarchyRoundTripsThroughTheDocumentFile)
    {
        EditorDocument source(Logging);
        EditorScene& scene = source.GetScene();
        const EntityId root = scene.CreateEntity(Vec3d{ 1.0f, 0.0f, 0.0f });
        const EntityId middle = scene.CreateEntity(Vec3d{ 0.0f, 2.0f, 0.0f });
        const EntityId leaf = scene.CreateEntity(Vec3d{ 0.0f, 0.0f, 3.0f });
        ASSERT_TRUE(scene.SetParent(middle, root));
        ASSERT_TRUE(scene.SetParent(leaf, middle));
        const PersistentEntityId rootId = IdOf(source, root);
        const PersistentEntityId middleId = IdOf(source, middle);
        const PersistentEntityId leafId = IdOf(source, leaf);

        EditorDocument loaded(Logging);
        ASSERT_TRUE(loaded.LoadFromJson(source.ToJson()));
        EditorScene& loadedScene = loaded.GetScene();

        const EntityId loadedRoot = FindById(loaded, rootId);
        const EntityId loadedMiddle = FindById(loaded, middleId);
        const EntityId loadedLeaf = FindById(loaded, leafId);
        ASSERT_TRUE(loadedRoot.IsValid());
        ASSERT_TRUE(loadedMiddle.IsValid());
        ASSERT_TRUE(loadedLeaf.IsValid());
        EXPECT_EQ(loadedScene.GetParent(loadedRoot), EntityId{});
        EXPECT_EQ(loadedScene.GetParent(loadedMiddle), loadedRoot);
        EXPECT_EQ(loadedScene.GetParent(loadedLeaf), loadedMiddle);

        // Locals are the authored values, so the composed world position
        // reproduces after the trip.
        loadedScene.RefreshDerivedTransforms();
        const Transform3f* world = loadedScene.TryGetWorldTransform(loadedLeaf);
        ASSERT_NE(world, nullptr);
        EXPECT_NEAR(world->Position.X, 1.0f, 1.0e-4f);
        EXPECT_NEAR(world->Position.Y, 2.0f, 1.0e-4f);
        EXPECT_NEAR(world->Position.Z, 3.0f, 1.0e-4f);
    }

    TEST_F(DocumentPersistenceTest, PassthroughSceneStripsEditorAnnotations)
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "sencha_persist_strip";
        fs::create_directories(root);

        EditorDocument doc(Logging);
        const EntityId entity = doc.GetScene().CreateEntity(Vec3d{});
        auto rename = MakeRenameEntityCommand(entity, "Editor Only Name",
                                              doc.GetScene(), doc);
        ASSERT_NE(rename, nullptr);
        rename->Execute();

        const std::optional<DocumentCookInput> input =
            CollectDocumentCookInput(doc, root, 16.0, Logging);
        ASSERT_TRUE(input.has_value());

        const JsonValue& scene = input->Snapshot().PassthroughScene;
        ASSERT_TRUE(scene.IsObject());
        const JsonValue* entities = scene.Find("entities");
        ASSERT_NE(entities, nullptr);
        for (const JsonValue& entityJson : entities->AsArray())
        {
            const JsonValue* components = entityJson.Find("components");
            if (components == nullptr || !components->IsObject())
                continue;
            for (const auto& [key, value] : components->AsObject())
            {
                EXPECT_NE(key, "name") << "authored names must not reach the cook";
                EXPECT_NE(key, "baked_brush");
            }
        }

        std::error_code ec;
        fs::remove_all(root, ec);
    }
} // namespace
