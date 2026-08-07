#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <core/identity/Id.h>
#include <core/logging/LoggingProvider.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>

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

// Overwrites every entity's serialized id, modelling content no editor mint
// produced (a hand-edited file, or one copied wholesale).
void SetPersistentIds(JsonValue& root, PersistentEntityId id)
{
    JsonValue* entities = root.Find("entities");
    if (entities == nullptr || !entities->IsArray())
        return;
    for (JsonValue& entity : entities->AsArray())
    {
        JsonValue* components = entity.Find("components");
        if (components == nullptr || !components->IsObject())
            continue;
        JsonValue* persistent = components->Find("persistent_id");
        if (persistent == nullptr || !persistent->IsObject())
            continue;
        auto& fields = persistent->AsObject();
        std::erase_if(fields, [](const auto& field) { return field.first == "id"; });
        fields.emplace_back("id", JsonValue(PersistentEntityIdToString(id)));
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

TEST_F(PersistentIdMintTest, FileWithoutIdentityIsRejected)
{
    // Identity is authored, so reading a file never mints it: a document that
    // arrives without identity is malformed content, and repairing it silently
    // would rewrite the user's file and feed the cook ids no source recorded.
    EditorScene& scene = Document.GetScene();
    (void)scene.CreateBrush(Vec3d{ 0, 0, 0 });
    (void)scene.CreateEntity(Vec3d{ 1, 0, 0 });

    JsonValue saved = Document.ToJson();
    StripPersistentIds(saved);

    LoggingProvider logging;
    EditorDocument loaded(logging);
    EXPECT_FALSE(loaded.LoadFromJson(saved));
    EXPECT_EQ(loaded.GetScene().GetEntityCount(), 0u)
        << "a rejected load must not leave half a document behind";
}

TEST_F(PersistentIdMintTest, AdoptionMintsForAnEntityAssembledInTheRegistry)
{
    // The path a migration takes when it builds an entity out of manifest data
    // rather than through the Create* helpers: identity comes from adoption, so
    // no route into the scene can leave an entity unidentified.
    EditorScene& scene = Document.GetScene();
    const EntityId entity = Document.GetScene().GetRegistry().Components.CreateEntity();
    scene.TrackEntity(entity);

    EXPECT_TRUE(IdOf(entity).IsValid());
}

TEST_F(PersistentIdMintTest, ReadoptingATrackedEntityKeepsItsId)
{
    EditorScene& scene = Document.GetScene();
    const EntityId entity = scene.CreateBrush(Vec3d{ 0, 0, 0 });
    const PersistentEntityId original = IdOf(entity);
    ASSERT_TRUE(original.IsValid());

    // Adoption settles identity, so it must stay idempotent: a second call has
    // to recognize the entity as the id's owner rather than as a collision.
    scene.TrackEntity(entity);

    EXPECT_EQ(IdOf(entity), original);
    EXPECT_EQ(scene.GetEntityCount(), 1u);
}

TEST_F(PersistentIdMintTest, ReservedNamespaceIdIsRejected)
{
    // Bit 63 belongs to the runtime allocator. Content holding one was not
    // minted by an editor, and accepting it would let authored content collide
    // with ids the runtime hands out for dynamically spawned entities.
    EditorScene& scene = Document.GetScene();
    (void)scene.CreateEntity(Vec3d{ 0, 0, 0 });

    JsonValue saved = Document.ToJson();
    SetPersistentIds(saved, PersistentEntityId{ PersistentEntityIdRuntimeBit | 0x1234 });

    LoggingProvider logging;
    EditorDocument loaded(logging);
    EXPECT_FALSE(loaded.LoadFromJson(saved));
}

TEST_F(PersistentIdMintTest, DuplicateIdsInOneFileAreRejected)
{
    // Two entities sharing an id would merge their recorded state in any save
    // overlay joined on identity, so the file is refused rather than repaired.
    EditorScene& scene = Document.GetScene();
    (void)scene.CreateEntity(Vec3d{ 0, 0, 0 });
    (void)scene.CreateEntity(Vec3d{ 1, 0, 0 });

    JsonValue saved = Document.ToJson();
    SetPersistentIds(saved, PersistentEntityId{ 0x0badc0de });

    LoggingProvider logging;
    EditorDocument loaded(logging);
    EXPECT_FALSE(loaded.LoadFromJson(saved));
}

TEST_F(PersistentIdMintTest, UnsetIdInAFileIsRejected)
{
    // The all-zero text form used to mean "unset, backfill me". Nothing mints on
    // load now, so it is simply malformed.
    EditorScene& scene = Document.GetScene();
    (void)scene.CreateEntity(Vec3d{ 0, 0, 0 });

    JsonValue saved = Document.ToJson();
    SetPersistentIds(saved, PersistentEntityId{});

    LoggingProvider logging;
    EditorDocument loaded(logging);
    EXPECT_FALSE(loaded.LoadFromJson(saved));
}

TEST_F(PersistentIdMintTest, BulkCreationAndReloadKeepIdsUnique)
{
    EditorScene& scene = Document.GetScene();
    constexpr int Count = 256;
    for (int i = 0; i < Count; ++i)
        (void)scene.CreateEntity(Vec3d{ static_cast<float>(i), 0, 0 });

    std::unordered_set<uint64_t> ids;
    for (const EntityId entity : scene.GetAllEntities())
        EXPECT_TRUE(ids.insert(IdOf(entity).Value).second);
    EXPECT_EQ(ids.size(), static_cast<size_t>(Count));

    // The reload exercises the index rebuild: a claim pass that saw its own
    // entries as collisions would remint everything and open the file dirty.
    // The destination starts dirty so the clean outcome is proven to come from
    // the load, not from the document's default state.
    LoggingProvider logging;
    EditorDocument loaded(logging);
    loaded.MarkDirty();
    ASSERT_TRUE(loaded.LoadFromJson(Document.ToJson()));
    EXPECT_FALSE(loaded.IsDirty());

    std::unordered_set<uint64_t> reloaded;
    for (const EntityId entity : loaded.GetScene().GetAllEntities())
    {
        const auto* component =
            loaded.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
        ASSERT_NE(component, nullptr);
        EXPECT_TRUE(reloaded.insert(component->Id.Value).second);
    }
    EXPECT_EQ(reloaded, ids);
}

TEST_F(PersistentIdMintTest, DestroyedIdIsAvailableToLaterMints)
{
    EditorScene& scene = Document.GetScene();
    const EntityId entity = scene.CreateEntity(Vec3d{ 0, 0, 0 });
    const PersistentEntityId original = IdOf(entity);
    const EntitySnapshot snapshot = Document.CaptureEntity(entity);

    scene.DestroyEntity(entity);
    // Nothing live holds the id now, so the restore is entitled to it. This is
    // what makes undo of a delete identity-preserving.
    const EntityId restored = Document.RestoreEntity(snapshot);
    EXPECT_EQ(IdOf(restored), original);

    // ...and a second restore of the same snapshot is a copy, not the original.
    const EntityId copy = Document.RestoreEntity(snapshot);
    EXPECT_NE(IdOf(copy), original);
    EXPECT_TRUE(IdOf(copy).IsValid());
}

TEST_F(PersistentIdMintTest, IdentityComponentIsNotRemovableByTheInspector)
{
    // The inspector's generic remove path (and, through the same trait, its add
    // menu) must not reach identity: stripping it would strand anything joined
    // on the id, and the saved document would be refused on its next load.
    const IComponentSerializer* identity = nullptr;
    for (const auto& serializer : EditorSceneSerializers().Entries())
        if (serializer->JsonKey() == "persistent_id")
            identity = serializer.get();

    ASSERT_NE(identity, nullptr) << "persistent_id must be a registered serializer";
    EXPECT_FALSE(identity->IsRemovable());
}
