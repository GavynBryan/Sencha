// Stage 6a (docs/assets/pipeline.md, Decision H): hot-reload detection +
// single-source re-import, the headless halves. The GPU swap half (texture
// bindless rewrite + deletion-queue retire) needs a device and is covered by
// the live CubeDemo gate, per the 4b/4c precedent.

#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <assets/cook/AssetImporter.h>
#include <assets/cook/CookedCache.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/hotreload/AssetSourceWatcher.h>
#include <core/logging/LoggingProvider.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
    class TempRoot
    {
    public:
        TempRoot()
        {
            std::random_device rd;
            Root = std::filesystem::temp_directory_path()
                 / ("sencha_hotreload_test_" + std::to_string(rd()));
            std::filesystem::create_directories(Root);
        }
        ~TempRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        void Write(std::string_view rel, std::string_view contents) const
        {
            const std::filesystem::path full = Root / rel;
            std::filesystem::create_directories(full.parent_path());
            std::ofstream f(full, std::ios::binary | std::ios::trunc);
            f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        }

        [[nodiscard]] std::string Str() const { return Root.generic_string(); }
        [[nodiscard]] const std::filesystem::path& Path() const { return Root; }

    private:
        std::filesystem::path Root;
    };

    // Bump a file's mtime forward so the watcher's mtime gate fires even when
    // the test runs faster than the filesystem timestamp resolution.
    void TouchFuture(const std::filesystem::path& p)
    {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(p, ec);
        std::filesystem::last_write_time(p, t + std::chrono::seconds(2), ec);
    }

    // A trivial importer for ".fake" sources: writes "<bytes>.cooked" under
    // .cooked/ as a single artifact. Exercises ReimportOneSource without a GPU.
    class FakeImporter final : public IAssetImporter
    {
    public:
        [[nodiscard]] std::vector<std::string_view> SourceExtensions() const override
        {
            return { ".fake" };
        }
        [[nodiscard]] ImportResult Import(const ImportInput& input,
                                          ICookOutputWriter& output) override
        {
            std::vector<std::byte> out(input.Bytes.begin(), input.Bytes.end());
            const std::string suffix = ".cooked";
            for (char c : suffix)
                out.push_back(static_cast<std::byte>(c));

            CookedArtifact artifact;
            artifact.Path = "asset://" + std::string(input.SourceRelPath);
            artifact.FileRelPath = ".cooked/" + std::string(input.SourceRelPath) + ".out";
            artifact.Type = AssetType::Texture;

            ImportResult result;
            if (!output.WriteBytes(artifact.FileRelPath, out))
                return ImportResult{ .Error = "fake importer: write failed" };
            result.Artifacts.push_back(std::move(artifact));
            return result;
        }
    };
} // namespace

TEST(AssetSourceWatcher, ReportsContentChangesNotTouches)
{
    TempRoot root;
    root.Write("textures/a.png", "AAAA");
    root.Write("textures/b.png", "BBBB");
    root.Write("meshes/c.glb", "CCCC"); // not a watched extension

    LoggingProvider logging;
    AssetSourceWatcher watcher(logging, root.Str(), { ".png" });
    watcher.Initialize();
    EXPECT_EQ(watcher.WatchCount(), 2u); // the two PNGs, not the glb

    // No change yet.
    EXPECT_TRUE(watcher.PollChanged().empty());

    // Real content change to a.png → reported once.
    root.Write("textures/a.png", "AAAA-EDITED");
    TouchFuture(root.Path() / "textures/a.png");
    const std::vector<std::string> changed = watcher.PollChanged();
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], "textures/a.png");

    // Polling again with no further change reports nothing (baseline updated).
    EXPECT_TRUE(watcher.PollChanged().empty());

    // A touch with identical content (mtime moves, bytes don't) is ignored.
    TouchFuture(root.Path() / "textures/b.png");
    EXPECT_TRUE(watcher.PollChanged().empty());
}

TEST(ReimportOneSource, RecooksAndReportsArtifacts)
{
    TempRoot root;
    root.Write("tex/x.fake", "hello");

    LoggingProvider logging;
    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));

    std::vector<std::string> artifacts;
    ASSERT_TRUE(ReimportOneSource(root.Str(), "tex/x.fake", importers, logging, artifacts));
    ASSERT_EQ(artifacts.size(), 1u);
    EXPECT_EQ(artifacts[0], "asset://tex/x.fake");

    // The cooked artifact was written with the importer's transform.
    const std::filesystem::path cooked = root.Path() / ".cooked/tex/x.fake.out";
    ASSERT_TRUE(std::filesystem::exists(cooked));
    std::ifstream in(cooked, std::ios::binary);
    const std::string body((std::istreambuf_iterator<char>(in)), {});
    EXPECT_EQ(body, "hello.cooked");

    // The cooked index now records the source as fresh, so a cold-start cook
    // would skip it.
    CookedCacheIndex index;
    std::string err;
    ASSERT_TRUE(CookedCacheIndex::LoadFromFile(
        (root.Path() / ".cooked/index.json").generic_string(), index, &err)) << err;
    const CookedSourceEntry* entry = index.Find("tex/x.fake");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->Artifacts.size(), 1u);
    EXPECT_EQ(entry->Artifacts[0].Path, "asset://tex/x.fake");

    // A re-edit recooks fresh content.
    root.Write("tex/x.fake", "world");
    artifacts.clear();
    ASSERT_TRUE(ReimportOneSource(root.Str(), "tex/x.fake", importers, logging, artifacts));
    std::ifstream in2(cooked, std::ios::binary);
    const std::string body2((std::istreambuf_iterator<char>(in2)), {});
    EXPECT_EQ(body2, "world.cooked");
}

TEST(ReimportOneSource, FailsCleanlyForUnknownExtension)
{
    TempRoot root;
    root.Write("tex/y.bogus", "data");

    LoggingProvider logging;
    AssetImporterRegistry importers; // empty

    std::vector<std::string> artifacts;
    EXPECT_FALSE(ReimportOneSource(root.Str(), "tex/y.bogus", importers, logging, artifacts));
    EXPECT_TRUE(artifacts.empty());
}

#endif // SENCHA_ENABLE_COOK
