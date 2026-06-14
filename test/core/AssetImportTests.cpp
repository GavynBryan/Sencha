#include <core/assets/AssetRegistry.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/AssetImporter.h>
#include <assets/cook/CookedCache.h>
#include <assets/cook/ImportOnDemand.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>
#endif

namespace
{
    class TempAssetRoot
    {
    public:
        TempAssetRoot()
        {
            std::random_device rd;
            Root = std::filesystem::temp_directory_path()
                 / ("sencha_asset_import_test_" + std::to_string(rd()));
            std::filesystem::create_directories(Root);
        }

        ~TempAssetRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        void WriteFile(std::string_view relPath, std::string_view contents) const
        {
            const std::filesystem::path full = Root / relPath;
            std::filesystem::create_directories(full.parent_path());
            std::ofstream file(full, std::ios::binary | std::ios::trunc);
            file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        }

        [[nodiscard]] std::string PathString() const { return Root.generic_string(); }
        [[nodiscard]] const std::filesystem::path& Path() const { return Root; }

    private:
        std::filesystem::path Root;
    };
} // namespace

// -- ScanAssetsDirectory: Stage 4a additions ---------------------------------

TEST(AssetScan, ScanFillsContentHash)
{
    TempAssetRoot root;
    const std::string contents = "not actually a mesh, but bytes are bytes";
    root.WriteFile("meshes/thing.smesh", contents);

    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(ScanAssetsDirectory(root.PathString(), registry));

    const AssetRecord* record = registry.FindByPath("asset://meshes/thing.smesh");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->ContentHash, HashBytes64(std::string_view(contents)));
}

TEST(AssetScan, ScanSkipsCookedCacheDirectory)
{
    TempAssetRoot root;
    root.WriteFile("meshes/real.smesh", "real");
    root.WriteFile(".cooked/meshes/cooked.smesh", "cooked artifact");
    root.WriteFile(".cooked/index.json", "{}");

    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(ScanAssetsDirectory(root.PathString(), registry));

    EXPECT_TRUE(registry.Contains("asset://meshes/real.smesh"));
    EXPECT_FALSE(registry.Contains("asset://.cooked/meshes/cooked.smesh"));
}

#ifdef SENCHA_ENABLE_COOK

namespace
{
    // Fake importer: cooks ".src" sources into a single ".smesh"-typed
    // artifact whose bytes are "cooked:" + source bytes. Counts invocations
    // so tests can pin when the cooked cache short-circuits the import.
    class FakeImporter final : public IAssetImporter
    {
    public:
        std::vector<std::string_view> SourceExtensions() const override { return { ".src" }; }

        ImportResult Import(const ImportInput& input, ICookOutputWriter& output) override
        {
            ++ImportCount;
            if (FailWith.has_value())
                return ImportResult{ .Error = *FailWith };

            std::string relPath(input.SourceRelPath);
            relPath.replace(relPath.rfind(".src"), 4, ".smesh");

            CookedArtifact artifact;
            artifact.Path = "asset://" + relPath;
            artifact.FileRelPath = (EscapeCookedDir ? "outside/" : ".cooked/") + relPath;
            artifact.Type = AssetType::StaticMesh;

            std::string cooked = "cooked:" + std::string(
                reinterpret_cast<const char*>(input.Bytes.data()), input.Bytes.size());
            if (!output.WriteBytes(artifact.FileRelPath,
                                   std::as_bytes(std::span(cooked.data(), cooked.size()))))
                return ImportResult{ .Error = "write failed" };

            ImportResult result;
            result.Artifacts.push_back(std::move(artifact));
            return result;
        }

        int ImportCount = 0;
        std::optional<std::string> FailWith;
        bool EscapeCookedDir = false;
    };
} // namespace

// -- CookedCacheIndex ---------------------------------------------------------

TEST(CookedCacheIndex, JsonRoundTripPreservesEntries)
{
    CookedSourceEntry entry;
    entry.SourceRelPath = "textures/dev/checker.png";
    entry.SourceHash = 0xDEADBEEFCAFEF00DULL; // high bits must survive JSON
    entry.Artifacts = {
        { "asset://textures/dev/checker.stex", ".cooked/textures/dev/checker.stex",
          AssetType::Texture },
        { "asset://textures/dev/checker_orm.stex", ".cooked/textures/dev/checker_orm.stex",
          AssetType::Texture },
    };

    CookedCacheIndex index;
    index.Put(entry);

    CookedCacheIndex parsed;
    std::string error;
    ASSERT_TRUE(CookedCacheIndex::FromJson(index.ToJson(), parsed, &error)) << error;
    ASSERT_EQ(parsed.Size(), 1u);

    const CookedSourceEntry* found = parsed.Find("textures/dev/checker.png");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->SourceHash, 0xDEADBEEFCAFEF00DULL);
    ASSERT_EQ(found->Artifacts.size(), 2u);
    EXPECT_EQ(found->Artifacts[0].Path, "asset://textures/dev/checker.stex");
    EXPECT_EQ(found->Artifacts[0].FileRelPath, ".cooked/textures/dev/checker.stex");
    EXPECT_EQ(found->Artifacts[0].Type, AssetType::Texture);
}

TEST(CookedCacheIndex, PutReplacesEntryForSameSource)
{
    CookedCacheIndex index;
    index.Put({ "a.src", 1, { { "asset://a.smesh", ".cooked/a.smesh", AssetType::StaticMesh } } });
    index.Put({ "a.src", 2, { { "asset://a.smesh", ".cooked/a.smesh", AssetType::StaticMesh } } });

    ASSERT_EQ(index.Size(), 1u);
    EXPECT_EQ(index.Find("a.src")->SourceHash, 2u);
}

TEST(CookedCacheIndex, FromJsonRejectsBadDocuments)
{
    CookedCacheIndex out;
    std::string error;

    EXPECT_FALSE(CookedCacheIndex::FromJson(JsonValue(3.0), out, &error));

    JsonValue::Object noVersion;
    noVersion.emplace_back("sources", JsonValue(JsonValue::Array{}));
    EXPECT_FALSE(CookedCacheIndex::FromJson(JsonValue(std::move(noVersion)), out, &error));

    JsonValue::Object badVersion;
    badVersion.emplace_back("version", JsonValue(999.0));
    badVersion.emplace_back("sources", JsonValue(JsonValue::Array{}));
    EXPECT_FALSE(CookedCacheIndex::FromJson(JsonValue(std::move(badVersion)), out, &error));

    // A source entry with a malformed hash.
    JsonValue::Object badEntry;
    badEntry.emplace_back("source", JsonValue(std::string("a.src")));
    badEntry.emplace_back("hash", JsonValue(std::string("nope")));
    badEntry.emplace_back("artifacts", JsonValue(JsonValue::Array{}));
    JsonValue::Array sources;
    sources.emplace_back(std::move(badEntry));
    JsonValue::Object badHash;
    badHash.emplace_back("version", JsonValue(1.0));
    badHash.emplace_back("sources", JsonValue(std::move(sources)));
    EXPECT_FALSE(CookedCacheIndex::FromJson(JsonValue(std::move(badHash)), out, &error));
}

// -- ImportAssetsOnDemand -------------------------------------------------------

TEST(ImportOnDemand, ColdCacheRunsImporterAndRegistersArtifacts)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rock.src", "rock source bytes");

    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));

    LoggingProvider logging;
    AssetRegistry registry(logging);
    ImportOnDemandStats stats;
    ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging, &stats));

    EXPECT_EQ(importer.ImportCount, 1);
    EXPECT_EQ(stats.SourcesSeen, 1u);
    EXPECT_EQ(stats.Imported, 1u);
    EXPECT_EQ(stats.CookedFresh, 0u);
    EXPECT_EQ(stats.Failed, 0u);

    const AssetRecord* record = registry.FindByPath("asset://meshes/rock.smesh");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Type, AssetType::StaticMesh);
    EXPECT_TRUE(record->FilePath.ends_with(".cooked/meshes/rock.smesh"));
    EXPECT_NE(record->ContentHash, 0u);

    // The cooked artifact and index landed on disk.
    EXPECT_TRUE(std::filesystem::is_regular_file(root.Path() / ".cooked/meshes/rock.smesh"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root.Path() / ".cooked/index.json"));
}

TEST(ImportOnDemand, WarmCacheSkipsImporter)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rock.src", "rock source bytes");

    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    LoggingProvider logging;

    {
        AssetRegistry registry(logging);
        ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging));
    }
    ASSERT_EQ(importer.ImportCount, 1);

    AssetRegistry registry(logging);
    ImportOnDemandStats stats;
    ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging, &stats));

    EXPECT_EQ(importer.ImportCount, 1) << "fresh cooked cache must not re-import";
    EXPECT_EQ(stats.CookedFresh, 1u);
    EXPECT_EQ(stats.Imported, 0u);
    EXPECT_TRUE(registry.Contains("asset://meshes/rock.smesh"));
}

TEST(ImportOnDemand, ChangedSourceRecooks)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rock.src", "version one");

    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    LoggingProvider logging;

    {
        AssetRegistry registry(logging);
        ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging));
    }

    root.WriteFile("meshes/rock.src", "version two");

    AssetRegistry registry(logging);
    ImportOnDemandStats stats;
    ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging, &stats));

    EXPECT_EQ(importer.ImportCount, 2);
    EXPECT_EQ(stats.Imported, 1u);
    EXPECT_EQ(stats.CookedFresh, 0u);
}

TEST(ImportOnDemand, MissingArtifactFileRecooks)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rock.src", "rock source bytes");

    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    LoggingProvider logging;

    {
        AssetRegistry registry(logging);
        ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging));
    }

    std::filesystem::remove(root.Path() / ".cooked/meshes/rock.smesh");

    AssetRegistry registry(logging);
    ImportOnDemandStats stats;
    ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging, &stats));

    EXPECT_EQ(importer.ImportCount, 2);
    EXPECT_EQ(stats.Imported, 1u);
    EXPECT_TRUE(std::filesystem::is_regular_file(root.Path() / ".cooked/meshes/rock.smesh"));
}

TEST(ImportOnDemand, ImporterErrorFailsThatSourceOnly)
{
    TempAssetRoot root;
    root.WriteFile("meshes/bad.src", "will fail");

    FakeImporter importer;
    importer.FailWith = "synthetic failure";
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));

    LoggingProvider logging;
    AssetRegistry registry(logging);
    ImportOnDemandStats stats;
    EXPECT_FALSE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging, &stats));

    EXPECT_EQ(stats.Failed, 1u);
    EXPECT_EQ(stats.Imported, 0u);
    EXPECT_FALSE(registry.Contains("asset://meshes/bad.smesh"));
}

TEST(ImportOnDemand, ArtifactOutsideCookedDirIsRejected)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rogue.src", "tries to escape");

    FakeImporter importer;
    importer.EscapeCookedDir = true;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));

    LoggingProvider logging;
    AssetRegistry registry(logging);
    ImportOnDemandStats stats;
    EXPECT_FALSE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging, &stats));

    EXPECT_EQ(stats.Failed, 1u);
    EXPECT_FALSE(registry.Contains("asset://meshes/rogue.smesh"));
}

TEST(ImportOnDemand, CorruptIndexIsColdCacheNotError)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rock.src", "rock source bytes");
    root.WriteFile(".cooked/index.json", "this is not json {{{");

    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));

    LoggingProvider logging;
    AssetRegistry registry(logging);
    ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging));

    EXPECT_EQ(importer.ImportCount, 1);
    EXPECT_TRUE(registry.Contains("asset://meshes/rock.smesh"));
}

TEST(ImportOnDemand, RegistryRejectsDuplicateExtensionClaims)
{
    FakeImporter first;
    FakeImporter second;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(first));
    EXPECT_FALSE(importers.Register(second));
    EXPECT_EQ(importers.FindByExtension(".src"), &first);
}

#endif // SENCHA_ENABLE_COOK
