#include "level/LevelCook.h"
#include "level/LevelDocument.h"
#include "level/LevelSerialization.h"

#include <assets/cook/CookPrune.h>
#include <assets/cook/CookedCache.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetManifest.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    class LevelCookTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterLevelSerializers(); }

        void SetUp() override
        {
            Root = fs::temp_directory_path()
                / ("sencha_levelcook_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            fs::remove_all(Root);
            fs::create_directories(Root);

            // A material on disk so the manifest can hash + resolve it.
            WriteFile("materials/dev/gray.smat", "{}");
        }
        void TearDown() override { std::error_code ec; fs::remove_all(Root, ec); }

        void WriteFile(const std::string& rel, std::string_view contents)
        {
            const fs::path p = Root / rel;
            fs::create_directories(p.parent_path());
            std::ofstream(p, std::ios::trunc) << contents;
        }

        // Authors a two-brush level (brushes far enough apart to land in separate
        // 16m cells) and saves it through the real LevelDocument save path.
        fs::path AuthorTwoBrushLevel()
        {
            LevelDocument doc;
            doc.SetDefaultMaterial(AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
            doc.GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
            doc.GetScene().CreateBrush(Vec3d{ 100, 0, 0 });

            const fs::path levelPath = Root / "levels/test.json";
            fs::create_directories(levelPath.parent_path());
            EXPECT_TRUE(doc.SaveAs(levelPath.generic_string()));
            return levelPath;
        }

        [[nodiscard]] JsonValue ReadJson(const fs::path& p) const
        {
            std::ifstream f(p);
            std::ostringstream buf;
            buf << f.rdbuf();
            return *JsonParse(buf.str());
        }

        fs::path Root;
    };
}

TEST_F(LevelCookTest, CooksPerCellMeshesAndScene)
{
    const fs::path levelPath = AuthorTwoBrushLevel();

    const LevelCookResult result = CookLevel(levelPath, Root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    // Two far-apart brushes -> two cells -> two Generated meshes, each on disk.
    EXPECT_EQ(result.CellCount, 2u);
    ASSERT_EQ(result.GeneratedMeshPaths.size(), 2u);
    for (const std::string& meshPath : result.GeneratedMeshPaths)
    {
        const std::string rel = meshPath.substr(std::string("asset://").size());
        EXPECT_TRUE(fs::exists(Root / ".cooked" / rel)) << meshPath;
    }

    EXPECT_TRUE(fs::exists(result.CookedScenePath));
    EXPECT_TRUE(fs::exists(result.ManifestPath));

    // The cooked scene is brush-free and carries a StaticMesh-per-cell with a
    // materials array (the per-cell, per-section binding).
    const JsonValue cooked = ReadJson(result.CookedScenePath);
    const JsonValue* entities = cooked.Find("entities");
    ASSERT_NE(entities, nullptr);
    ASSERT_TRUE(entities->IsArray());

    std::size_t staticMeshEntities = 0;
    for (const JsonValue& entity : entities->AsArray())
    {
        const JsonValue* components = entity.Find("components");
        ASSERT_NE(components, nullptr);
        EXPECT_EQ(components->Find("brush"), nullptr); // no brushes survive the cook
        if (const JsonValue* sm = components->Find("StaticMesh"))
        {
            ++staticMeshEntities;
            ASSERT_NE(sm->Find("materials"), nullptr);
            EXPECT_TRUE(sm->Find("materials")->IsArray());
        }
    }
    EXPECT_EQ(staticMeshEntities, 2u);
}

TEST_F(LevelCookTest, CooksLiveDocumentWithoutSavingOrMutatingIt)
{
    // A never-saved document: a brush plus a non-brush entity (a camera) that must
    // pass through the cook unchanged, standing in for any game component entity.
    LevelDocument doc;
    doc.SetDefaultMaterial(AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
    doc.GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    const EntityId camera = doc.GetScene().CreateCamera(Vec3d{ 0, 2, 5 });
    const std::size_t entityCountBefore = doc.GetScene().GetAllEntities().size();

    const LevelCookResult result = CookLevel(doc, "live", Root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;
    EXPECT_EQ(result.CellCount, 1u);

    // The live overload snapshots internally: the editor's document is untouched
    // (its brush and camera both survive, unlike the throwaway the cook mutated).
    EXPECT_EQ(doc.GetScene().GetAllEntities().size(), entityCountBefore);
    EXPECT_TRUE(doc.GetScene().HasEntity(camera));

    // The cooked scene drops brushes, emits one cell StaticMesh, and passes the
    // camera entity through.
    const JsonValue cooked = ReadJson(result.CookedScenePath);
    const JsonValue* entities = cooked.Find("entities");
    ASSERT_NE(entities, nullptr);
    ASSERT_TRUE(entities->IsArray());

    int cameras = 0, staticMeshes = 0, brushes = 0;
    for (const JsonValue& entity : entities->AsArray())
    {
        const JsonValue* components = entity.Find("components");
        if (components == nullptr)
            continue;
        cameras += components->Find("Camera") != nullptr;
        staticMeshes += components->Find("StaticMesh") != nullptr;
        brushes += components->Find("brush") != nullptr;
    }
    EXPECT_EQ(cameras, 1);
    EXPECT_EQ(staticMeshes, 1);
    EXPECT_EQ(brushes, 0);
}

TEST_F(LevelCookTest, CookedSceneFullyResolvesAgainstScannedRegistry)
{
    const fs::path levelPath = AuthorTwoBrushLevel();
    ASSERT_TRUE(CookLevel(levelPath, Root, 16.0).Success);

    // The runtime's resolution inputs: scan the authored assets and the cooked
    // overlay, then bind ids. Scanning .cooked/ as its own root maps each
    // generated mesh to the exact asset:// path the cook stamped, because
    // MakeVirtualAssetPath is relative to the scanned root. The authored scan
    // skips .cooked/ on its own, so the two roots don't collide.
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ScanAssetsDirectory(Root.generic_string(), registry);
    ScanAssetsDirectory((Root / ".cooked").generic_string(), registry);

    AssetIdMap idMap;
    ASSERT_TRUE(AssetIdMap::LoadFromFile((Root / kAssetIdMapFileName).generic_string(), idMap));
    ApplyAssetIds(idMap, registry);

    // Every asset:// the cooked scene references must resolve to a registered
    // record carrying the id the cook assigned. This is the headless proof that a
    // host can find everything a cooked level points at (the registration gate),
    // independent of GPU-backed mesh/material residency.
    const JsonValue cooked = ReadJson(Root / ".cooked/levels/test.cooked.json");
    const std::vector<std::string> refs = CollectAssetPaths(cooked);
    ASSERT_FALSE(refs.empty());

    for (const std::string& ref : refs)
    {
        const AssetRecord* record = registry.FindByPath(ref);
        ASSERT_NE(record, nullptr) << "unresolved ref: " << ref;
        EXPECT_TRUE(record->Id.IsValid()) << ref;
        EXPECT_EQ(record->Id, idMap.FindId(ref)) << ref;
    }

    // The generated cell meshes specifically are discovered from the overlay.
    EXPECT_NE(registry.FindByPath("asset://levels/test/cell_0_0_0.smesh"), nullptr);
    EXPECT_NE(registry.FindByPath("asset://levels/test/cell_6_0_0.smesh"), nullptr);
}

TEST_F(LevelCookTest, RecookKeepsStableIds)
{
    const fs::path levelPath = AuthorTwoBrushLevel();

    ASSERT_TRUE(CookLevel(levelPath, Root, 16.0).Success);
    AssetIdMap first;
    ASSERT_TRUE(AssetIdMap::LoadFromFile((Root / kAssetIdMapFileName).generic_string(), first));

    ASSERT_TRUE(CookLevel(levelPath, Root, 16.0).Success);
    AssetIdMap second;
    ASSERT_TRUE(AssetIdMap::LoadFromFile((Root / kAssetIdMapFileName).generic_string(), second));

    const std::string meshPath = "asset://levels/test/cell_0_0_0.smesh";
    EXPECT_TRUE(first.FindId(meshPath).IsValid());
    EXPECT_EQ(first.FindId(meshPath), second.FindId(meshPath));
    EXPECT_EQ(first.FindId("asset://materials/dev/gray.smat"),
              second.FindId("asset://materials/dev/gray.smat"));
}

TEST_F(LevelCookTest, PruneRemovesArtifactsWhenLevelDeleted)
{
    const fs::path levelPath = AuthorTwoBrushLevel();
    const LevelCookResult result = CookLevel(levelPath, Root, 16.0);
    ASSERT_TRUE(result.Success);

    fs::remove(levelPath); // the level was deleted from the project

    CookedCacheIndex index;
    ASSERT_TRUE(CookedCacheIndex::LoadFromFile((Root / ".cooked/index.json").generic_string(), index));
    const std::size_t pruned = PruneOrphanedGeneratedArtifacts(index, Root);
    EXPECT_EQ(pruned, 1u);
    EXPECT_EQ(index.Find("levels/test.json"), nullptr);

    for (const std::string& meshPath : result.GeneratedMeshPaths)
    {
        const std::string rel = meshPath.substr(std::string("asset://").size());
        EXPECT_FALSE(fs::exists(Root / ".cooked" / rel)) << meshPath;
    }
}
