#include <core/assets/AssetId.h>
#include <core/assets/AssetRegistry.h>
#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetManifest.h>
#include <core/hash/ContentHash.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <render/MaterialCache.h>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

// -- AssetId text form -------------------------------------------------------

TEST(AssetId, HexRoundTrip)
{
    const AssetId id{ 0x0123456789abcdefull };
    const std::string text = AssetIdToString(id);
    EXPECT_EQ(text, "0123456789abcdef");

    const std::optional<AssetId> parsed = AssetIdFromString(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, id);
}

TEST(AssetId, ParseRejectsMalformedText)
{
    EXPECT_FALSE(AssetIdFromString("").has_value());
    EXPECT_FALSE(AssetIdFromString("123").has_value());                 // too short
    EXPECT_FALSE(AssetIdFromString("0123456789abcdef00").has_value());  // too long
    EXPECT_FALSE(AssetIdFromString("0123456789abcdeg").has_value());    // bad digit
    EXPECT_FALSE(AssetIdFromString("0123456789ABCDEF").has_value());    // uppercase
    EXPECT_FALSE(AssetIdFromString("-123456789abcdef").has_value());    // sign
    EXPECT_FALSE(AssetIdFromString("0000000000000000").has_value());    // the invalid id
}

// -- Registry id binding -------------------------------------------------------

namespace
{
    AssetRecord MakeRecord(std::string path, AssetType type = AssetType::StaticMesh)
    {
        AssetRecord record;
        record.Type = type;
        record.SourceKind = AssetSourceKind::File;
        record.Path = std::move(path);
        record.FilePath = "ignored.bin";
        return record;
    }
}

TEST(AssetRegistryIds, AssignAndFindById)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(registry.Register(MakeRecord("asset://meshes/cube.smesh")));

    const AssetId id{ 0xa1 };
    EXPECT_EQ(registry.FindById(id), nullptr);
    ASSERT_TRUE(registry.AssignId("asset://meshes/cube.smesh", id));
    EXPECT_TRUE(registry.AssignId("asset://meshes/cube.smesh", id));  // idempotent

    const AssetRecord* record = registry.FindById(id);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Path, "asset://meshes/cube.smesh");
    EXPECT_EQ(record->Id, id);
    EXPECT_EQ(registry.FindByPath("asset://meshes/cube.smesh")->Id, id);
}

TEST(AssetRegistryIds, AssignRebindsButNeverTakesAnotherAssetsId)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(registry.Register(MakeRecord("asset://a.smesh")));
    ASSERT_TRUE(registry.Register(MakeRecord("asset://b.smesh")));

    EXPECT_FALSE(registry.AssignId("asset://a.smesh", AssetId{}));            // invalid id
    EXPECT_FALSE(registry.AssignId("asset://missing.smesh", AssetId{ 1 }));   // unknown path

    ASSERT_TRUE(registry.AssignId("asset://a.smesh", AssetId{ 1 }));
    ASSERT_TRUE(registry.AssignId("asset://a.smesh", AssetId{ 2 }));   // rebinding
    EXPECT_EQ(registry.FindById(AssetId{ 1 }), nullptr);
    EXPECT_FALSE(registry.AssignId("asset://b.smesh", AssetId{ 2 }));  // id already bound
    EXPECT_EQ(registry.FindById(AssetId{ 2 })->Path, "asset://a.smesh");
}

TEST(AssetRegistryIds, ARecordThatStatesNoIdHasItsPathsAndATakenOneIsNotShared)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(registry.Register(MakeRecord("asset://a.smesh")));
    EXPECT_EQ(registry.FindByPath("asset://a.smesh")->Id, AssetIdOfPath("asset://a.smesh"));

    AssetRecord copy = MakeRecord("asset://copy.smesh");
    copy.Id = AssetIdOfPath("asset://a.smesh");
    ASSERT_TRUE(registry.Register(copy));
    EXPECT_EQ(registry.FindById(copy.Id)->Path, "asset://a.smesh") << "the first holder keeps it";
    EXPECT_NE(registry.FindByPath("asset://copy.smesh")->Id, copy.Id);
}

// -- Id-first resolution through the front door ---------------------------------

TEST(AssetSystemIds, ResolveRefPathPrefersIdAndFallsBackToPath)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    AssetSystem assets(logging, registry);

    ASSERT_TRUE(registry.Register(MakeRecord("asset://meshes/renamed.smesh")));
    const AssetId meshId{ 0x77 };
    ASSERT_TRUE(registry.AssignId("asset://meshes/renamed.smesh", meshId));

    // Known id: the record's current path wins over the stale stamped path.
    EXPECT_EQ(assets.ResolveRefPath(meshId, "asset://meshes/old.smesh", AssetType::StaticMesh),
              "asset://meshes/renamed.smesh");

    // Unknown id and invalid id: the stamped path is the answer.
    EXPECT_EQ(assets.ResolveRefPath(AssetId{ 0xdead }, "asset://meshes/old.smesh",
                                    AssetType::StaticMesh),
              "asset://meshes/old.smesh");
    EXPECT_EQ(assets.ResolveRefPath(AssetId{}, "asset://meshes/old.smesh",
                                    AssetType::StaticMesh),
              "asset://meshes/old.smesh");

    // Type mismatch: logged and the path falls back rather than mis-loading.
    EXPECT_EQ(assets.ResolveRefPath(meshId, "asset://materials/fallback.smat",
                                    AssetType::Material),
              "asset://materials/fallback.smat");
}

// A reference follows its asset: a known id gives the current path, a known
// path gives the id, and what the registry does not know stays as written.
TEST(AssetRegistryIds, AReferenceIsMadeCurrentByItsIdOrItsPath)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(registry.Register(MakeRecord("asset://moved.smesh")));
    const AssetId id = registry.FindByPath("asset://moved.smesh")->Id;

    const AssetRef stale = registry.Current(AssetRef{ AssetType::StaticMesh, "asset://old.smesh", id });
    EXPECT_EQ(stale.Path, "asset://moved.smesh");

    const AssetRef bare = registry.Current(AssetRef{ AssetType::StaticMesh, "asset://moved.smesh" });
    EXPECT_EQ(bare.Id, id);

    const AssetRef unknown = registry.Current(AssetRef{ AssetType::StaticMesh, "asset://gone.smesh", AssetId{ 9 } });
    EXPECT_EQ(unknown.Path, "asset://gone.smesh");
    EXPECT_EQ(unknown.Id, AssetId{ 9 });
}

// What was read before a move still finds the asset by the path it was at.
TEST(AssetRegistryIds, APathAnAssetMovedFromStillFindsIt)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(registry.Register(MakeRecord("asset://old.smesh")));
    const AssetId id = registry.FindByPath("asset://old.smesh")->Id;
    ASSERT_TRUE(registry.Unregister("asset://old.smesh"));
    AssetRecord moved = MakeRecord("asset://new.smesh");
    moved.Id = id;
    ASSERT_TRUE(registry.Register(moved));

    ASSERT_NE(registry.Find(AssetRef{ AssetType::StaticMesh, "asset://old.smesh" }), nullptr);
    EXPECT_EQ(registry.Find(AssetRef{ AssetType::StaticMesh, "asset://old.smesh" })->Path, "asset://new.smesh");
    const AssetRef current = registry.Current(AssetRef{ AssetType::StaticMesh, "asset://old.smesh" });
    EXPECT_EQ(current.Path, "asset://new.smesh");
    EXPECT_EQ(current.Id, id);
    EXPECT_FALSE(registry.Contains("asset://old.smesh")) << "nothing is at the old path";

    ASSERT_TRUE(registry.Unregister("asset://new.smesh"));
    EXPECT_EQ(registry.Find(AssetRef{ AssetType::StaticMesh, "asset://old.smesh" }), nullptr) << "gone is gone";
}

// A dependency named by id is found where its asset now is.
TEST(AssetRegistryIds, AReferenceFindsItsAssetByIdBeforePath)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(registry.Register(MakeRecord("asset://moved.smesh")));
    ASSERT_TRUE(registry.Register(MakeRecord("asset://other.smesh")));
    const AssetId moved = registry.FindByPath("asset://moved.smesh")->Id;

    EXPECT_EQ(registry.Find(AssetRef{ AssetType::StaticMesh, "asset://other.smesh", moved })->Path,
              "asset://moved.smesh") << "the id decides";
    EXPECT_EQ(registry.Find(AssetRef{ AssetType::StaticMesh, "asset://other.smesh" })->Path, "asset://other.smesh");
    EXPECT_EQ(registry.Find(AssetRef{ AssetType::StaticMesh, "asset://gone.smesh", AssetId{ 7 } }), nullptr);
}

// The snapshot a stage reads is the registry as it was asked for, and asking
// again after a change gives the change.
TEST(AssetRegistryIds, TheIdSnapshotIsTakenAfterEachChange)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(registry.Register(MakeRecord("asset://a.smesh")));
    const std::shared_ptr<const AssetPathsById> first = registry.PathsById();
    EXPECT_EQ(registry.PathsById(), first) << "unchanged, the same snapshot";
    EXPECT_EQ(first->at(AssetIdOfPath("asset://a.smesh")), "asset://a.smesh");

    ASSERT_TRUE(registry.AssignId("asset://a.smesh", AssetId{ 0x42 }));
    const std::shared_ptr<const AssetPathsById> second = registry.PathsById();
    EXPECT_NE(second, first);
    EXPECT_EQ(second->at(AssetId{ 0x42 }), "asset://a.smesh");
    EXPECT_FALSE(second->contains(AssetIdOfPath("asset://a.smesh")));
    EXPECT_EQ(first->size(), 1u) << "a snapshot already taken never changes";
}
