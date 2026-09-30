#include <gtest/gtest.h>

#include <assets/cook/ContentImporters.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/hotreload/SourceReloadRoots.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/ContentMount.h>
#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackageCache.h>
#include <core/logging/LoggingProvider.h>
#include <jobs/AsyncTaskQueue.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

//=============================================================================
// The save-and-look loop, the one assembly the runtime and every editor use.
//
// What is pinned: a changed stylesheet re-cooks the document that imports it
// and the resident package moves to a new version once the drain runs; the
// poll is throttled to its interval; a file that appeared after the roots
// were added is watched after a rescan and not before; and adding a root a
// second time widens what it watches instead of watching it twice.
//=============================================================================

namespace
{
    void WriteText(const std::filesystem::path& path, std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    }

    // The watcher gates on mtime before hashing; two writes inside one
    // filesystem timestamp tick would look like none.
    void BumpMtime(const std::filesystem::path& path)
    {
        std::error_code ec;
        const auto now = std::filesystem::last_write_time(path, ec);
        std::filesystem::last_write_time(path, now + std::chrono::seconds(2), ec);
    }

    struct Fixture
    {
        std::filesystem::path Root;
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets;
        AsyncTaskQueue Tasks{ 0 };
        AssetLease Package;

        Fixture()
            : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
        {
            std::random_device rd;
            Root = std::filesystem::temp_directory_path()
                 / ("sencha_source_reload_" + std::to_string(rd()));
            WriteText(Root / "ui" / "a.rml",
                      "<rml><head><link type=\"text/rcss\" href=\"a.rcss\"/></head>"
                      "<body><div id=\"box\"/></body></rml>");
            WriteText(Root / "ui" / "a.rcss", "#box { display: block; width: 10px; height: 10px; }");

            // Cooked into this stack, the way a content root is mounted.
            const ContentRootPaths paths = ResolveContentRoot(Root.generic_string());
            ScanContentRoot(paths, Assets);
            ContentImporterSet importers;
            (void)ImportAssetsOnDemand(Root.generic_string(), importers.Registry(), Assets.Registry, Logging);
            RegisterCookedContent(paths, Assets, Logging.GetLogger<Fixture>());
            Package = Assets.Assets.LoadLease("asset://ui/a.rml", AssetType::UiPackage);
        }
        ~Fixture()
        {
            Package = AssetLease{};
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        [[nodiscard]] std::uint64_t Version() const
        {
            return Assets.UiPackages.GetReloadVersion(
                UiPackageHandle::FromToken(Package.OpaqueToken()));
        }
        void Drain()
        {
            (void)Tasks.PumpWork();
            (void)Tasks.DrainCompletions();
        }
    };

    using Clock = std::chrono::steady_clock;
}

TEST(SourceReloadRoots, AChangedStylesheetRecooksTheDocumentThatImportsIt)
{
    Fixture f;
    ASSERT_TRUE(f.Package.IsValid()) << "the fixture's document did not cook";
    const std::uint64_t before = f.Version();

    SourceReloadRoots roots(f.Logging, nullptr, f.Tasks, f.Assets.Assets, f.Assets.Registry);
    roots.AddRoot(f.Root.generic_string(), { ".rml", ".rcss" });
    EXPECT_EQ(roots.RootCount(), 1u);
    EXPECT_EQ(roots.WatchedFileCount(), 2u);

    WriteText(f.Root / "ui" / "a.rcss", "#box { display: block; width: 20px; height: 20px; }");
    BumpMtime(f.Root / "ui" / "a.rcss");

    const Clock::time_point t0 = Clock::now();
    EXPECT_EQ(roots.Poll(t0), 1u) << "the stylesheet edit was not seen";
    f.Drain();
    EXPECT_GT(f.Version(), before)
        << "the document importing the sheet was not re-cooked and swapped in place";
}

TEST(SourceReloadRoots, ThePollIsThrottledToItsInterval)
{
    Fixture f;
    SourceReloadRoots roots(f.Logging, nullptr, f.Tasks, f.Assets.Assets, f.Assets.Registry);
    roots.AddRoot(f.Root.generic_string(), { ".rml", ".rcss" });

    const Clock::time_point t0 = Clock::now();
    EXPECT_EQ(roots.Poll(t0), 0u) << "nothing changed yet";

    WriteText(f.Root / "ui" / "a.rcss", "#box { display: block; width: 30px; height: 30px; }");
    BumpMtime(f.Root / "ui" / "a.rcss");
    EXPECT_EQ(roots.Poll(t0 + std::chrono::milliseconds(10)), 0u)
        << "polled inside the interval";
    EXPECT_EQ(roots.Poll(t0 + roots.Interval), 1u) << "the interval elapsed and the change is seen";
}

TEST(SourceReloadRoots, AFileCreatedLaterIsWatchedAfterARescan)
{
    Fixture f;
    SourceReloadRoots roots(f.Logging, nullptr, f.Tasks, f.Assets.Assets, f.Assets.Registry);
    roots.AddRoot(f.Root.generic_string(), { ".rml", ".rcss" });
    ASSERT_EQ(roots.WatchedFileCount(), 2u);

    WriteText(f.Root / "ui" / "b.rml", "<rml><body/></rml>");
    EXPECT_EQ(roots.WatchedFileCount(), 2u) << "a new file is not discovered by itself";
    roots.Rescan();
    EXPECT_EQ(roots.WatchedFileCount(), 3u);
}

TEST(SourceReloadRoots, ReloadingByHandNamesARootItKnows)
{
    Fixture f;
    SourceReloadRoots roots(f.Logging, nullptr, f.Tasks, f.Assets.Assets, f.Assets.Registry);
    roots.AddRoot(f.Root.generic_string(), {});
    EXPECT_EQ(roots.WatchedFileCount(), 0u) << "no extensions, nothing watched";

    const std::uint64_t before = f.Version();
    EXPECT_FALSE(roots.ReloadSource("/nowhere", "ui/a.rml"));
    EXPECT_TRUE(roots.ReloadSource(f.Root.generic_string(), "ui/a.rml"));
    f.Drain();
    EXPECT_GT(f.Version(), before);
}

TEST(SourceReloadRoots, AddingARootAgainWidensItsExtensions)
{
    Fixture f;
    SourceReloadRoots roots(f.Logging, nullptr, f.Tasks, f.Assets.Assets, f.Assets.Registry);
    roots.AddRoot(f.Root.generic_string(), { ".rml" });
    ASSERT_EQ(roots.WatchedFileCount(), 1u);

    roots.AddRoot(f.Root.generic_string(), { ".rcss" });
    EXPECT_EQ(roots.RootCount(), 1u) << "the same root is watched once";
    EXPECT_EQ(roots.WatchedFileCount(), 2u) << "the second call widened the first's extensions";
}
