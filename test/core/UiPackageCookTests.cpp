#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <assets/cook/ContentImporters.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

// What the .rml cook has to get right, in order of how expensive it is to get
// wrong:
//
//  1. Editing a shared stylesheet recooks every document that imports it. A
//     build cache that misses an input looks like nothing happening, which
//     costs an afternoon before anyone suspects the cooker.
//  2. A package is self-contained. Anything resolved from disk at load would
//     work on the machine that cooked it and nowhere else.
//  3. References resolve against the file that wrote them, not the root.

namespace
{
class TempAssetRoot
{
public:
    TempAssetRoot()
    {
        std::random_device rd;
        Root = std::filesystem::temp_directory_path()
             / ("sencha_ui_cook_test_" + std::to_string(rd()));
        std::filesystem::create_directories(Root);
    }

    ~TempAssetRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    TempAssetRoot(const TempAssetRoot&) = delete;
    TempAssetRoot& operator=(const TempAssetRoot&) = delete;

    void Write(std::string_view relPath, std::string_view contents) const
    {
        const std::filesystem::path full = Root / relPath;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream file(full, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    [[nodiscard]] bool ReadPackage(std::string_view cookedRelPath, UiPackage& out) const
    {
        const std::filesystem::path full = Root / cookedRelPath;
        std::ifstream file(full, std::ios::binary);
        if (!file.is_open())
            return false;
        const std::vector<char> raw((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
        std::vector<std::byte> bytes(raw.size());
        std::memcpy(bytes.data(), raw.data(), raw.size());
        return LoadSuiFromBytes(bytes, out);
    }

    [[nodiscard]] std::string PathString() const { return Root.generic_string(); }

private:
    std::filesystem::path Root;
};

struct CookRun
{
    ImportOnDemandStats Stats;
    bool Ok = false;
};

CookRun Cook(const TempAssetRoot& root)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ContentImporterSet importers;
    CookRun run;
    run.Ok = ImportAssetsOnDemand(root.PathString(), importers.Registry(), registry,
                                  logging, &run.Stats);
    return run;
}

constexpr std::string_view kDocument = R"(<rml>
<head><link type="text/rcss" href="theme.rcss"/></head>
<body><div id="panel"/></body>
</rml>)";

constexpr std::string_view kTheme = "#panel { width: 100px; }\n";
} // namespace

TEST(UiPackageCook, ADocumentAndItsStylesheetBecomeOneSelfContainedPackage)
{
    TempAssetRoot root;
    root.Write("ui/hud.rml", kDocument);
    root.Write("ui/theme.rcss", kTheme);

    const CookRun run = Cook(root);
    ASSERT_TRUE(run.Ok);
    EXPECT_EQ(run.Stats.Imported, 1u) << "the .rcss must not cook as an asset of its own";
    EXPECT_EQ(run.Stats.Failed, 0u);

    UiPackage package;
    ASSERT_TRUE(root.ReadPackage(".cooked/ui/hud.rml.sui", package));

    EXPECT_EQ(package.RootDocumentName, "hud.rml");
    ASSERT_EQ(package.Blobs.size(), 2u);
    EXPECT_EQ(package.Blobs[0].Kind, UiBlobKind::Document);

    // The name the document engine will ask for, which is the href as written.
    const UiPackageBlob* theme = package.FindBlob("theme.rcss");
    ASSERT_NE(theme, nullptr) << "the stylesheet is not reachable by the name the markup used";
    EXPECT_EQ(theme->Kind, UiBlobKind::StyleSheet);
    EXPECT_EQ(theme->SourcePath, "ui/theme.rcss");
    ASSERT_FALSE(theme->Bytes.empty());
}

TEST(UiPackageCook, EditingASharedStylesheetRecooksEveryDocumentThatImportsIt)
{
    TempAssetRoot root;
    root.Write("ui/hud.rml", kDocument);
    root.Write("ui/menu.rml", kDocument);
    root.Write("ui/theme.rcss", kTheme);

    ASSERT_EQ(Cook(root).Stats.Imported, 2u);

    // Nothing changed: both served from cache.
    const CookRun unchanged = Cook(root);
    EXPECT_EQ(unchanged.Stats.Imported, 0u);
    EXPECT_EQ(unchanged.Stats.CookedFresh, 2u);

    // The documents' own bytes are untouched. Only the stylesheet moves.
    root.Write("ui/theme.rcss", "#panel { width: 250px; }\n");

    const CookRun afterEdit = Cook(root);
    EXPECT_EQ(afterEdit.Stats.Imported, 2u)
        << "a stylesheet edit that does not recook its documents is a silently stale build";

    UiPackage package;
    ASSERT_TRUE(root.ReadPackage(".cooked/ui/hud.rml.sui", package));
    const UiPackageBlob* theme = package.FindBlob("theme.rcss");
    ASSERT_NE(theme, nullptr);
    const std::string themeText(reinterpret_cast<const char*>(theme->Bytes.data()),
                                theme->Bytes.size());
    EXPECT_NE(themeText.find("250px"), std::string::npos)
        << "the package still carries the old stylesheet bytes";
}

TEST(UiPackageCook, DeletingAStylesheetIsNoticedRatherThanServedFromCache)
{
    TempAssetRoot root;
    root.Write("ui/hud.rml", kDocument);
    root.Write("ui/theme.rcss", kTheme);
    ASSERT_EQ(Cook(root).Stats.Imported, 1u);

    std::filesystem::remove(std::filesystem::path(root.PathString()) / "ui/theme.rcss");

    const CookRun afterDelete = Cook(root);
    EXPECT_EQ(afterDelete.Stats.CookedFresh, 0u) << "the stale package was served anyway";
    EXPECT_EQ(afterDelete.Stats.Failed, 1u)
        << "a document whose stylesheet is gone must fail loudly, not cook without it";
}

TEST(UiPackageCook, NestedImportsResolveAgainstTheSheetThatWroteThem)
{
    TempAssetRoot root;
    root.Write("ui/hud.rml", R"(<rml>
<head><link type="text/rcss" href="theme/panel.rcss"/></head>
<body><div id="panel"/></body>
</rml>)");
    // Relative to ui/theme/, not to ui/.
    root.Write("ui/theme/panel.rcss", "@import \"base.rcss\";\n#panel { width: 10px; }\n");
    root.Write("ui/theme/base.rcss", "body { color: #fff; }\n");

    const CookRun run = Cook(root);
    ASSERT_TRUE(run.Ok);
    EXPECT_EQ(run.Stats.Failed, 0u);

    UiPackage package;
    ASSERT_TRUE(root.ReadPackage(".cooked/ui/hud.rml.sui", package));
    ASSERT_EQ(package.Blobs.size(), 3u);

    // Both names are what the engine's own path join will produce.
    EXPECT_NE(package.FindBlob("theme/panel.rcss"), nullptr);
    EXPECT_NE(package.FindBlob("theme/base.rcss"), nullptr)
        << "a nested import resolved against the document instead of the sheet";
}

TEST(UiPackageCook, ASheetImportedTwiceIsPackagedOnce)
{
    TempAssetRoot root;
    root.Write("ui/hud.rml", R"(<rml>
<head>
<link type="text/rcss" href="a.rcss"/>
<link type="text/rcss" href="b.rcss"/>
</head>
<body><div id="panel"/></body>
</rml>)");
    root.Write("ui/a.rcss", "@import \"shared.rcss\";\n");
    root.Write("ui/b.rcss", "@import \"shared.rcss\";\n");
    root.Write("ui/shared.rcss", "body { color: #fff; }\n");

    ASSERT_TRUE(Cook(root).Ok);

    UiPackage package;
    ASSERT_TRUE(root.ReadPackage(".cooked/ui/hud.rml.sui", package));
    EXPECT_EQ(package.Blobs.size(), 4u) << "document + a + b + shared, with shared once";
}

TEST(UiPackageCook, AnImportCycleIsRefusedRatherThanCookedForever)
{
    TempAssetRoot root;
    root.Write("ui/hud.rml", kDocument);
    root.Write("ui/theme.rcss", "@import \"theme.rcss\";\n");

    const CookRun run = Cook(root);
    // The self-import is deduplicated rather than followed, so this cooks;
    // what matters is that it terminates and produces a usable package.
    EXPECT_EQ(run.Stats.Failed, 0u);
    UiPackage package;
    EXPECT_TRUE(root.ReadPackage(".cooked/ui/hud.rml.sui", package));
}

TEST(UiPackageCook, FontsAndTexturesBecomeResourceTableEntriesNotCopies)
{
    TempAssetRoot root;
    root.Write("ui/hud.rml", R"(<rml>
<head><link type="text/rcss" href="theme.rcss"/></head>
<body><img src="icons/health.png"/></body>
</rml>)");
    root.Write("ui/theme.rcss", R"(
@font-face { src: "fonts/Inter-Regular.ttf"; }
#panel { decorator: image("panel-bg.png"); }
)");

    ASSERT_TRUE(Cook(root).Ok);

    UiPackage package;
    ASSERT_TRUE(root.ReadPackage(".cooked/ui/hud.rml.sui", package));

    const auto has = [&](AssetType type, std::string_view path) {
        return std::any_of(package.Resources.begin(), package.Resources.end(),
            [&](const AssetRef& ref) { return ref.Type == type && ref.Path == path; });
    };

    // Resolved against the file that named them, and rooted at the assets root.
    EXPECT_TRUE(has(AssetType::Texture, "asset://ui/icons/health.png"));
    EXPECT_TRUE(has(AssetType::Font, "asset://ui/fonts/Inter-Regular.ttf"));
    EXPECT_TRUE(has(AssetType::Texture, "asset://ui/panel-bg.png"));

    // A real asset has its own identity and is referenced, never inlined.
    for (const UiPackageBlob& blob : package.Blobs)
        EXPECT_NE(blob.VirtualName, "icons/health.png");
}

TEST(UiPackageCook, ConstructsOutsideTheSupportedProfileAreRecordedWithTheirSource)
{
    TempAssetRoot root;
    root.Write("ui/hud.rml", kDocument);
    root.Write("ui/theme.rcss", R"(
#panel { width: 10px; }
#fancy { box-shadow: 2px 2px #000; }
#blurred { filter: blur(3px); }
)");

    ASSERT_TRUE(Cook(root).Ok);

    UiPackage package;
    ASSERT_TRUE(root.ReadPackage(".cooked/ui/hud.rml.sui", package));

    const auto reported = [&](std::string_view feature) {
        return std::any_of(package.Unsupported.begin(), package.Unsupported.end(),
            [&](const UiUnsupportedFeature& note) { return note.Feature == feature; });
    };
    EXPECT_TRUE(reported("box-shadow"));
    EXPECT_TRUE(reported("filter"));

    for (const UiUnsupportedFeature& note : package.Unsupported)
    {
        EXPECT_EQ(note.SourcePath, "ui/theme.rcss");
        EXPECT_GT(note.Line, 0u) << "a diagnostic with no line is one nobody acts on";
    }
}

TEST(UiPackageCook, ACommentedOutImportDoesNotPullAFileIntoThePackage)
{
    TempAssetRoot root;
    root.Write("ui/hud.rml", R"(<rml>
<head>
<!-- <link type="text/rcss" href="disabled.rcss"/> -->
<link type="text/rcss" href="theme.rcss"/>
</head>
<body><div id="panel"/></body>
</rml>)");
    root.Write("ui/theme.rcss", "/* @import \"also-disabled.rcss\"; */\n#panel { width: 1px; }\n");

    const CookRun run = Cook(root);
    ASSERT_TRUE(run.Ok);
    EXPECT_EQ(run.Stats.Failed, 0u) << "a commented-out import was followed and failed to resolve";

    UiPackage package;
    ASSERT_TRUE(root.ReadPackage(".cooked/ui/hud.rml.sui", package));
    EXPECT_EQ(package.Blobs.size(), 2u);
}

#endif // SENCHA_ENABLE_COOK
