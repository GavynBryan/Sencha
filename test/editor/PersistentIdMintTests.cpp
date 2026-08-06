#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <core/identity/Id.h>
#include <core/logging/LoggingProvider.h>
#include <world/identity/PersistentIdComponent.h>

#include <string>
#include <unordered_set>

namespace
{

class PersistentIdMintTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    PersistentIdMintTest()
        : Document(Logging)
    {
    }

    [[nodiscard]] PersistentEntityId IdOf(EntityId entity) const
    {
        const auto* component =
            Document.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
        return component != nullptr ? component->Id : PersistentEntityId{};
    }

    LoggingProvider Logging;
    EditorDocument Document;
};

void StripPersistentIds(JsonValue& root)
{
    JsonValue* entities = root.Find("entities");
    if (entities == nullptr || !entities->IsArray())
        return;
    for (JsonValue& entity : entities->AsArray())
    {
        JsonValue* components = entity.Find("components");
        if (components == nullptr || !components->IsObject())
            continue;
        auto& members = components->AsObject();
        std::erase_if(members, [](const auto& member)
                      { return member.first == "persistent_id"; });
    }
}

} // namespace

TEST_F(PersistentIdMintTest, CreationMintsUniqueEditorNamespaceIds)
{
    EditorScene& scene = Document.GetScene();
    const EntityId brush = scene.CreateBrush(Vec3d{ 0, 0, 0 });
    const EntityId camera = scene.CreateCamera(Vec3d{ 1, 0, 0 });
    const EntityId entity = scene.CreateEntity(Vec3d{ 2, 0, 0 });

    std::unordered_set<uint64_t> seen;
    for (const EntityId created : { brush, camera, entity })
    {
        const PersistentEntityId id = IdOf(created);
        ASSERT_TRUE(id.IsValid());
        EXPECT_EQ(id.Value & PersistentEntityIdRuntimeBit, 0u)
            << "editor mints must keep the runtime namespace bit clear";
        EXPECT_TRUE(seen.insert(id.Value).second);
    }
}

TEST_F(PersistentIdMintTest, DuplicateMintsFreshId)
{
    EditorScene& scene = Document.GetScene();
    const EntityId source = scene.CreateBrush(Vec3d{ 0, 0, 0 });
    const EntityId copy = Document.DuplicateEntity(source);

    ASSERT_TRUE(IdOf(copy).IsValid());
    EXPECT_NE(IdOf(copy), IdOf(source));
}

TEST_F(PersistentIdMintTest, UndoOfDeleteRestoresTheSameId)
{
    EditorScene& scene = Document.GetScene();
    const EntityId entity = scene.CreateBrush(Vec3d{ 0, 0, 0 });
    const PersistentEntityId original = IdOf(entity);
    ASSERT_TRUE(original.IsValid());

    const EntitySnapshot snapshot = Document.CaptureEntity(entity);
    scene.DestroyEntity(entity);
    const EntityId restored = Document.RestoreEntity(snapshot);

    EXPECT_EQ(IdOf(restored), original);
}

TEST_F(PersistentIdMintTest, SnapshotWithoutIdMintsOnRestore)
{
    EditorScene& scene = Document.GetScene();
    const EntityId entity = scene.CreateEntity(Vec3d{ 0, 0, 0 });

    EntitySnapshot snapshot = Document.CaptureEntity(entity);
    auto& members = snapshot.Components.AsObject();
    std::erase_if(members, [](const auto& member)
                  { return member.first == "persistent_id"; });
    scene.DestroyEntity(entity);

    const EntityId restored = Document.RestoreEntity(snapshot);
    EXPECT_TRUE(IdOf(restored).IsValid());
}

TEST_F(PersistentIdMintTest, RoundTripPreservesIdsAndLoadsClean)
{
    EditorScene& scene = Document.GetScene();
    const EntityId first = scene.CreateBrush(Vec3d{ 0, 0, 0 });
    const EntityId second = scene.CreateEntity(Vec3d{ 1, 0, 0 });
    const PersistentEntityId firstId = IdOf(first);
    const PersistentEntityId secondId = IdOf(second);

    const JsonValue saved = Document.ToJson();

    LoggingProvider logging;
    EditorDocument loaded(logging);
    ASSERT_TRUE(loaded.LoadFromJson(saved));
    EXPECT_FALSE(loaded.IsDirty());

    std::unordered_set<uint64_t> ids;
    for (const EntityId entity : loaded.GetScene().GetAllEntities())
    {
        const auto* component =
            loaded.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
        ASSERT_NE(component, nullptr);
        ids.insert(component->Id.Value);
    }
    EXPECT_TRUE(ids.contains(firstId.Value));
    EXPECT_TRUE(ids.contains(secondId.Value));
}

TEST_F(PersistentIdMintTest, LegacyFileBackfillsAndOpensDirty)
{
    EditorScene& scene = Document.GetScene();
    (void)scene.CreateBrush(Vec3d{ 0, 0, 0 });
    (void)scene.CreateEntity(Vec3d{ 1, 0, 0 });

    JsonValue saved = Document.ToJson();
    StripPersistentIds(saved);

    LoggingProvider logging;
    EditorDocument loaded(logging);
    ASSERT_TRUE(loaded.LoadFromJson(saved));
    EXPECT_TRUE(loaded.IsDirty())
        << "a migrated legacy file must open dirty so minted ids reach disk";

    std::unordered_set<uint64_t> ids;
    for (const EntityId entity : loaded.GetScene().GetAllEntities())
    {
        const auto* component =
            loaded.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
        ASSERT_NE(component, nullptr);
        ASSERT_TRUE(component->Id.IsValid());
        EXPECT_TRUE(ids.insert(component->Id.Value).second);
    }
    EXPECT_EQ(ids.size(), 2u);
}
