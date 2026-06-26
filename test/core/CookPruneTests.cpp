#include <assets/cook/CookPrune.h>
#include <assets/cook/CookedCache.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    class CookPruneTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Root = fs::temp_directory_path()
                / ("sencha_prune_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            fs::remove_all(Root);
            fs::create_directories(Root);
        }
        void TearDown() override { std::error_code ec; fs::remove_all(Root, ec); }

        void Touch(const std::string& relPath)
        {
            const fs::path p = Root / relPath;
            fs::create_directories(p.parent_path());
            std::ofstream(p, std::ios::trunc) << "x";
        }

        // Registers a source with one Generated artifact, both backed by files.
        void AddSource(const std::string& sourceRel, const std::string& artifactRel)
        {
            Touch(sourceRel);
            Touch(artifactRel);
            CookedSourceEntry entry;
            entry.SourceRelPath = sourceRel;
            entry.SourceHash = 1;
            entry.Artifacts.push_back(CookedArtifact{
                "asset://" + artifactRel, artifactRel, AssetType::StaticMesh });
            Index.Put(std::move(entry));
        }

        fs::path Root;
        CookedCacheIndex Index;
    };
}

TEST_F(CookPruneTest, DeadSourceLosesArtifactAndEntry)
{
    AddSource("levels/old.json", ".cooked/levels/old/cell_0_0_0.smesh");
    fs::remove(Root / "levels/old.json"); // the level was deleted

    const std::size_t pruned = PruneOrphanedGeneratedArtifacts(Index, Root);

    EXPECT_EQ(pruned, 1u);
    EXPECT_EQ(Index.Find("levels/old.json"), nullptr);
    EXPECT_FALSE(fs::exists(Root / ".cooked/levels/old/cell_0_0_0.smesh"));
}

TEST_F(CookPruneTest, LiveSourceIsUntouched)
{
    AddSource("levels/hub.json", ".cooked/levels/hub/cell_0_0_0.smesh");

    const std::size_t pruned = PruneOrphanedGeneratedArtifacts(Index, Root);

    EXPECT_EQ(pruned, 0u);
    EXPECT_NE(Index.Find("levels/hub.json"), nullptr);
    EXPECT_TRUE(fs::exists(Root / ".cooked/levels/hub/cell_0_0_0.smesh"));
}

TEST_F(CookPruneTest, CustomLivenessPredicateOverridesExistence)
{
    // The source file exists, but the predicate reports it dead (e.g. a level
    // that no longer contains any brushes, §6).
    AddSource("levels/empty.json", ".cooked/levels/empty/cell_0_0_0.smesh");

    const std::size_t pruned = PruneOrphanedGeneratedArtifacts(
        Index, Root, [](std::string_view) { return false; });

    EXPECT_EQ(pruned, 1u);
    EXPECT_EQ(Index.Find("levels/empty.json"), nullptr);
    EXPECT_FALSE(fs::exists(Root / ".cooked/levels/empty/cell_0_0_0.smesh"));
}

TEST_F(CookPruneTest, PrunesOnlyDeadAmongMany)
{
    AddSource("levels/a.json", ".cooked/levels/a/m.smesh");
    AddSource("levels/b.json", ".cooked/levels/b/m.smesh");
    AddSource("levels/c.json", ".cooked/levels/c/m.smesh");
    fs::remove(Root / "levels/b.json");

    const std::size_t pruned = PruneOrphanedGeneratedArtifacts(Index, Root);

    EXPECT_EQ(pruned, 1u);
    EXPECT_NE(Index.Find("levels/a.json"), nullptr);
    EXPECT_EQ(Index.Find("levels/b.json"), nullptr);
    EXPECT_NE(Index.Find("levels/c.json"), nullptr);
}
