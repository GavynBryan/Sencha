#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <render/static_mesh/StaticMeshHandle.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include "HandleFieldIo.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

//=============================================================================
// The reference-only asset composition: a process that must carry a mesh
// reference through without being able to hold the mesh. The headless cook
// reads an authored scene and writes the cooked one, and the mesh field has
// to survive that trip as the path it was authored with, without a GPU and
// without reading a byte of geometry.
//=============================================================================
namespace
{
    // A registry record that names a file nothing ever wrote. Loading it
    // through a real loader would fail on the read; through a reference store
    // it succeeds, which is the proof that no read happens.
    void RegisterUnreadable(AssetRegistry& registry, AssetType type, std::string_view path)
    {
        AssetRecord record;
        record.Path = std::string(path);
        record.FilePath = "/nonexistent/sencha-reference-store-test/" + std::string(path.substr(8));
        record.Type = type;
        record.SourceKind = AssetSourceKind::File;
        registry.Register(record);
    }
}

TEST(AssetReferenceStore, ReferenceOnlyCompositionHoldsTheMeshKindsAsIdentity)
{
    LoggingProvider logging;
    ComponentSerializerRegistry sceneSerializers;
    RegisterEngineSceneSerializers(sceneSerializers);
    RuntimeAssets assets(logging, sceneSerializers, RuntimeAssets::ReferenceOnly{});

    // The two kinds scene content names directly are loadable here, and the
    // caches that would hold their bytes are still absent.
    EXPECT_TRUE(assets.Assets.HasStore(AssetType::StaticMesh));
    EXPECT_TRUE(assets.Assets.HasStore(AssetType::SkinnedMesh));
    EXPECT_EQ(assets.StaticMeshes, nullptr);
    EXPECT_EQ(assets.SkinnedMeshes, nullptr);

    // Textures reach scene content only through materials, which already
    // load without them; nothing interns a texture.
    EXPECT_FALSE(assets.Assets.HasStore(AssetType::Texture));
    EXPECT_EQ(assets.Textures, nullptr);
}

TEST(AssetReferenceStore, LoadingInternsThePathWithoutReadingTheAsset)
{
    LoggingProvider logging;
    ComponentSerializerRegistry sceneSerializers;
    RegisterEngineSceneSerializers(sceneSerializers);
    RuntimeAssets assets(logging, sceneSerializers, RuntimeAssets::ReferenceOnly{});
    RegisterUnreadable(assets.Registry, AssetType::StaticMesh, "asset://meshes/dev/cube.smesh");

    AssetLease first = assets.Assets.LoadLease("asset://meshes/dev/cube.smesh", AssetType::StaticMesh);
    ASSERT_TRUE(first.IsValid());
    EXPECT_EQ(assets.Assets.GetPathForLease(AssetType::StaticMesh, first.OpaqueToken()),
              "asset://meshes/dev/cube.smesh");

    // Deduplicated like every cache: the same path is the same handle.
    AssetLease second = assets.Assets.LoadLease("asset://meshes/dev/cube.smesh", AssetType::StaticMesh);
    ASSERT_TRUE(second.IsValid());
    EXPECT_EQ(second.OpaqueToken(), first.OpaqueToken());

    // And released like every cache: once the last reference goes, the path
    // is no longer resident and the old handle names nothing.
    const std::uint64_t token = first.OpaqueToken();
    first.Reset();
    EXPECT_TRUE(assets.Assets.TryAcquireLease("asset://meshes/dev/cube.smesh", AssetType::StaticMesh).IsValid());
    second.Reset();
    EXPECT_FALSE(assets.Assets.TryAcquireLease("asset://meshes/dev/cube.smesh", AssetType::StaticMesh).IsValid());
    EXPECT_TRUE(assets.Assets.GetPathForLease(AssetType::StaticMesh, token).empty());
}

// A path the registry has never seen is still a missing asset. The store
// holds identity, not permission to invent it.
TEST(AssetReferenceStore, AnUnregisteredPathStillFailsToLoad)
{
    LoggingProvider logging;
    ComponentSerializerRegistry sceneSerializers;
    RegisterEngineSceneSerializers(sceneSerializers);
    RuntimeAssets assets(logging, sceneSerializers, RuntimeAssets::ReferenceOnly{});

    EXPECT_FALSE(assets.Assets.LoadLease("asset://meshes/dev/missing.smesh", AssetType::StaticMesh).IsValid());
}

// The whole reason the composition exists: a scene's mesh field loads to a
// handle and saves back to the path it came from. Under the headless
// composition the same load declines and leaves nothing to save.
TEST(AssetReferenceStore, AMeshFieldRoundTripsThroughTheSceneCodec)
{
    LoggingProvider logging;
    ComponentSerializerRegistry sceneSerializers;
    RegisterEngineSceneSerializers(sceneSerializers);
    RuntimeAssets assets(logging, sceneSerializers, RuntimeAssets::ReferenceOnly{});
    RegisterUnreadable(assets.Registry, AssetType::StaticMesh, "asset://meshes/dev/cube.smesh");

    auto parsed = JsonParse(R"("asset://meshes/dev/cube.smesh")");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(logging, &assets.Assets);
    JsonReadArchive reader(*parsed);
    StaticMeshHandle loaded;
    ASSERT_TRUE(LoadHandleField<StaticMeshHandle>(AssetType::StaticMesh, reader, "", loaded, context));
    ASSERT_TRUE(reader.Ok());
    ASSERT_TRUE(loaded.IsValid()) << "the reference store resolves what the headless composition declines";

    JsonWriteArchive writer;
    ASSERT_TRUE(SaveHandleField<StaticMeshHandle>(AssetType::StaticMesh, writer, "mesh", loaded, context));
    JsonValue json = writer.TakeValue();
    ASSERT_TRUE(json.IsString());
    EXPECT_EQ(json.AsString(), "asset://meshes/dev/cube.smesh");

    // The load's reference was relinquished into the handle; give it back.
    assets.Assets.ReleaseLease(AssetType::StaticMesh, loaded.ToToken());
}
