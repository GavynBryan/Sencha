#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
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

// -- AssetIdMap assignment ---------------------------------------------------

TEST(AssetIdMap, FirstSightMintsDeterministicallyFromPath)
{
    const std::string path = "asset://meshes/dev/cube.smesh";

    AssetIdMap a;
    AssetIdMap b;
    const AssetId fromA = a.EnsureId(path, 0x1111);
    const AssetId fromB = b.EnsureId(path, 0x2222);

    EXPECT_TRUE(fromA.IsValid());
    EXPECT_EQ(fromA, fromB);                       // independent maps agree
    EXPECT_EQ(fromA.Value, HashBytes64(path));     // and the scheme is the path hash
    EXPECT_EQ(a.EnsureId(path, 0x1111), fromA);    // idempotent
}

TEST(AssetIdMap, RenameInheritsIdViaContentHash)
{
    constexpr uint64_t contentHash = 0xfeedface;

    AssetIdMap map;
    const AssetId original = map.EnsureId("asset://meshes/old.smesh", contentHash);

    const auto onlyNewPathLives = [](std::string_view path) {
        return path == "asset://meshes/new.smesh";
    };
    const AssetId inherited =
        map.EnsureId("asset://meshes/new.smesh", contentHash, onlyNewPathLives);

    EXPECT_EQ(inherited, original);
    EXPECT_FALSE(map.FindId("asset://meshes/old.smesh").IsValid());  // donor entry retired
    EXPECT_EQ(map.FindId("asset://meshes/new.smesh"), original);
}

TEST(AssetIdMap, LiveDuplicateContentMintsFreshId)
{
    constexpr uint64_t contentHash = 0xfeedface;
    const auto everythingLives = [](std::string_view) { return true; };

    AssetIdMap map;
    const AssetId first = map.EnsureId("asset://meshes/a.smesh", contentHash, everythingLives);
    const AssetId second = map.EnsureId("asset://meshes/b.smesh", contentHash, everythingLives);

    // A copied file is a new asset, not a rename: the original still exists.
    EXPECT_NE(first, second);
    EXPECT_EQ(map.FindId("asset://meshes/a.smesh"), first);
}

TEST(AssetIdMap, MintProbesPastOccupiedId)
{
    const std::string path = "asset://meshes/cube.smesh";
    const AssetId pathHashId{ HashBytes64(path) };

    // Pre-bind the path-hash id to a different path (a synthetic collision —
    // real XXH64 collisions are not craftable in a test) and check the mint
    // probes to a different, valid id instead of double-binding.
    const std::string mapJson = std::string(R"({"version": 1, "assets": [)")
        + R"({"id": ")" + AssetIdToString(pathHashId)
        + R"(", "path": "asset://other/asset.smat", "content_hash": "0000000000000001"}]})";
    const std::optional<JsonValue> json = JsonParse(mapJson);
    ASSERT_TRUE(json.has_value());

    AssetIdMap map;
    std::string error;
    ASSERT_TRUE(AssetIdMap::FromJson(*json, map, &error)) << error;

    const AssetId minted = map.EnsureId(path, 0x2);
    EXPECT_TRUE(minted.IsValid());
    EXPECT_NE(minted, pathHashId);
    EXPECT_EQ(map.FindId("asset://other/asset.smat"), pathHashId);
}

// -- AssetIdMap persistence ----------------------------------------------------

TEST(AssetIdMap, JsonRoundTripAndDirtyTracking)
{
    AssetIdMap map;
    EXPECT_FALSE(map.IsDirty());
    const AssetId meshId = map.EnsureId("asset://meshes/dev/cube.smesh", 0xaa);
    const AssetId matId = map.EnsureId("asset://materials/dev/red.smat", 0xbb);
    EXPECT_TRUE(map.IsDirty());

    AssetIdMap parsed;
    std::string error;
    ASSERT_TRUE(AssetIdMap::FromJson(map.ToJson(), parsed, &error)) << error;
    EXPECT_FALSE(parsed.IsDirty());
    EXPECT_EQ(parsed.FindId("asset://meshes/dev/cube.smesh"), meshId);
    EXPECT_EQ(parsed.FindId("asset://materials/dev/red.smat"), matId);
    EXPECT_EQ(parsed.Entries().size(), 2u);

    // Re-ensuring with an unchanged hash must not dirty a loaded map — the
    // committed file only changes when identity actually changes.
    (void)parsed.EnsureId("asset://meshes/dev/cube.smesh", 0xaa);
    EXPECT_FALSE(parsed.IsDirty());
    (void)parsed.EnsureId("asset://meshes/dev/cube.smesh", 0xcc);
    EXPECT_TRUE(parsed.IsDirty());
}

TEST(AssetIdMap, SerializationIsSortedByPath)
{
    AssetIdMap map;
    (void)map.EnsureId("asset://z/last.smesh", 0x1);
    (void)map.EnsureId("asset://a/first.smesh", 0x2);

    const JsonValue json = map.ToJson();
    const JsonValue* assets = json.Find("assets");
    ASSERT_NE(assets, nullptr);
    ASSERT_EQ(assets->Size(), 2u);
    EXPECT_EQ(assets->AsArray()[0].Find("path")->AsString(), "asset://a/first.smesh");
    EXPECT_EQ(assets->AsArray()[1].Find("path")->AsString(), "asset://z/last.smesh");
}

TEST(AssetIdMap, ParseRejectsMangledMaps)
{
    const auto reject = [](std::string_view text) {
        const std::optional<JsonValue> json = JsonParse(text);
        ASSERT_TRUE(json.has_value());
        AssetIdMap map;
        EXPECT_FALSE(AssetIdMap::FromJson(*json, map)) << text;
    };

    reject(R"({"version": 9, "assets": []})");
    reject(R"({"version": 1, "assets": [{"id": "bogus", "path": "asset://a.smesh", "content_hash": "0000000000000000"}]})");
    reject(R"({"version": 1, "assets": [{"id": "0000000000000001", "path": "no_prefix", "content_hash": "0000000000000000"}]})");
    // Duplicate path and duplicate id are hand-mangled maps; refuse both.
    reject(R"({"version": 1, "assets": [
        {"id": "0000000000000001", "path": "asset://a.smesh", "content_hash": "0000000000000000"},
        {"id": "0000000000000002", "path": "asset://a.smesh", "content_hash": "0000000000000000"}]})");
    reject(R"({"version": 1, "assets": [
        {"id": "0000000000000001", "path": "asset://a.smesh", "content_hash": "0000000000000000"},
        {"id": "0000000000000001", "path": "asset://b.smesh", "content_hash": "0000000000000000"}]})");
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

TEST(AssetRegistryIds, AssignRejectsConflicts)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(registry.Register(MakeRecord("asset://a.smesh")));
    ASSERT_TRUE(registry.Register(MakeRecord("asset://b.smesh")));

    EXPECT_FALSE(registry.AssignId("asset://a.smesh", AssetId{}));            // invalid id
    EXPECT_FALSE(registry.AssignId("asset://missing.smesh", AssetId{ 1 }));   // unknown path

    ASSERT_TRUE(registry.AssignId("asset://a.smesh", AssetId{ 1 }));
    EXPECT_FALSE(registry.AssignId("asset://a.smesh", AssetId{ 2 }));  // reassignment
    EXPECT_FALSE(registry.AssignId("asset://b.smesh", AssetId{ 1 }));  // id already bound
    EXPECT_EQ(registry.FindById(AssetId{ 1 })->Path, "asset://a.smesh");
}

TEST(AssetRegistryIds, ApplyAssetIdsFillsRegisteredPathsOnly)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(registry.Register(MakeRecord("asset://meshes/cube.smesh")));

    AssetIdMap map;
    const AssetId known = map.EnsureId("asset://meshes/cube.smesh", 0x1);
    (void)map.EnsureId("asset://meshes/not_registered.smesh", 0x2);

    EXPECT_EQ(ApplyAssetIds(map, registry), 1u);
    EXPECT_EQ(registry.FindById(known)->Path, "asset://meshes/cube.smesh");
}

// -- Id-first resolution through the front door ---------------------------------

TEST(AssetSystemIds, ResolveRefPathPrefersIdAndFallsBackToPath)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);

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

// -- Stamping cooked JSON --------------------------------------------------------

TEST(AssetIdStamping, StampsKnownRefsAndLeavesTheRestAlone)
{
    AssetIdMap map;
    const AssetId meshId = map.EnsureId("asset://meshes/dev/cube.smesh", 0x1);

    const std::optional<JsonValue> scene = JsonParse(R"({
        "entities": [{ "components": {
            "StaticMesh": { "mesh": "asset://meshes/dev/cube.smesh",
                            "material": "asset://materials/dev/unmapped.smat",
                            "visible": true } } }]
    })");
    ASSERT_TRUE(scene.has_value());

    const JsonValue stamped = StampAssetRefIds(*scene, map);
    const JsonValue* component =
        stamped.Find("entities")->AsArray()[0].Find("components")->Find("StaticMesh");
    ASSERT_NE(component, nullptr);

    const JsonValue* mesh = component->Find("mesh");
    ASSERT_NE(mesh, nullptr);
    ASSERT_TRUE(mesh->IsObject());
    EXPECT_EQ(mesh->Find("id")->AsString(), AssetIdToString(meshId));
    EXPECT_EQ(mesh->Find("path")->AsString(), "asset://meshes/dev/cube.smesh");

    // Unmapped refs stay plain strings; non-ref fields are untouched.
    EXPECT_TRUE(component->Find("material")->IsString());
    EXPECT_TRUE(component->Find("visible")->IsBool());

    // Manifest derivation still sees every path in the stamped document —
    // the {"id","path"} object keeps the path string walkable.
    const std::vector<std::string> paths = CollectAssetPaths(stamped);
    const std::unordered_set<std::string> pathSet(paths.begin(), paths.end());
    EXPECT_TRUE(pathSet.contains("asset://meshes/dev/cube.smesh"));
    EXPECT_TRUE(pathSet.contains("asset://materials/dev/unmapped.smat"));
}
