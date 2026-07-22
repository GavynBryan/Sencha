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
            LastMeta.assign(reinterpret_cast<const char*>(input.MetaBytes.data()),
                            input.MetaBytes.size());
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
        std::string LastMeta;
        std::optional<std::string> FailWith;
        bool EscapeCookedDir = false;
    };
} // namespace

// -- CookedCacheIndex ---------------------------------------------------------

TEST(CookedCacheIndex, JsonRoundTripPreservesEntries)
{
    CookedSourceEntry entry;
    entry.SourceRelPath = "textures/dev/checker.png";
    entry.InputFingerprint = 0xDEADBEEFCAFEF00DULL; // high bits must survive JSON
    entry.SourceSize = 0xFFFFFFFF12345678ULL; // > 2^53: doubles would mangle it
    entry.SourceMTime = -1234567890123456789LL;
    entry.MetaSize = 42;
    entry.MetaMTime = 987654321;
    entry.Artifacts = {
        { "asset://textures/dev/checker.stex", ".cooked/textures/dev/checker.stex",
          AssetType::Texture, 0xABCDEF0123456789ULL },
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
    EXPECT_EQ(found->InputFingerprint, 0xDEADBEEFCAFEF00DULL);
    EXPECT_EQ(found->SourceSize, 0xFFFFFFFF12345678ULL);
    EXPECT_EQ(found->SourceMTime, -1234567890123456789LL);
    EXPECT_EQ(found->MetaSize, 42u);
    EXPECT_EQ(found->MetaMTime, 987654321);
    ASSERT_EQ(found->Artifacts.size(), 2u);
    EXPECT_EQ(found->Artifacts[0].Path, "asset://textures/dev/checker.stex");
    EXPECT_EQ(found->Artifacts[0].FileRelPath, ".cooked/textures/dev/checker.stex");
    EXPECT_EQ(found->Artifacts[0].Type, AssetType::Texture);
    EXPECT_EQ(found->Artifacts[0].ContentHash, 0xABCDEF0123456789ULL);
    EXPECT_EQ(found->Artifacts[1].ContentHash, 0u);
}

TEST(CookedCacheIndex, PutReplacesEntryForSameSource)
{
    CookedCacheIndex index;
    index.Put({ .SourceRelPath = "a.src", .InputFingerprint = 1,
                .Artifacts = { { "asset://a.smesh", ".cooked/a.smesh", AssetType::StaticMesh } } });
    index.Put({ .SourceRelPath = "a.src", .InputFingerprint = 2,
                .Artifacts = { { "asset://a.smesh", ".cooked/a.smesh", AssetType::StaticMesh } } });

    ASSERT_EQ(index.Size(), 1u);
    EXPECT_EQ(index.Find("a.src")->InputFingerprint, 2u);
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

TEST(ImportOnDemand, TouchedUnchangedSourceStaysFresh)
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

    // Rewrite identical content: new mtime, same bytes. The stat fast path
    // misses, the hash fallback must still serve from cache.
    root.WriteFile("meshes/rock.src", "rock source bytes");

    AssetRegistry registry(logging);
    ImportOnDemandStats stats;
    ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging, &stats));

    EXPECT_EQ(importer.ImportCount, 1) << "unchanged content must not re-import";
    EXPECT_EQ(stats.CookedFresh, 1u);
    EXPECT_EQ(stats.Imported, 0u);
}

TEST(ImportOnDemand, RegistrationUsesRecordedArtifactHashes)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rock.src", "rock source bytes");

    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    LoggingProvider logging;

    uint64_t cookedHash = 0;
    {
        AssetRegistry registry(logging);
        ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging));
        const AssetRecord* record = registry.FindByPath("asset://meshes/rock.smesh");
        ASSERT_NE(record, nullptr);
        cookedHash = record->ContentHash;
        ASSERT_NE(cookedHash, 0u);
    }

    // Tamper with the artifact bytes (file still exists, source untouched).
    // Registration trusts the hash recorded at cook time instead of reading
    // the artifact back, so the record keeps the cooked hash.
    root.WriteFile(".cooked/meshes/rock.smesh", "tampered bytes");

    AssetRegistry registry(logging);
    ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging));
    EXPECT_EQ(importer.ImportCount, 1);

    const AssetRecord* record = registry.FindByPath("asset://meshes/rock.smesh");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->ContentHash, cookedHash);
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

// -- Prepare / publish / register split ---------------------------------------

namespace
{
    // Captures what a prepared import forwards, so a test can prove publication
    // routes only through the publisher and never touches active state itself.
    class RecordingPublisher final : public IImportPublisher
    {
    public:
        bool WriteBytes(std::string_view fileRelPath,
                        std::span<const std::byte> bytes) override
        {
            Writes.emplace_back(
                std::string(fileRelPath),
                std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            return true;
        }

        void PutIndexEntry(CookedSourceEntry entry) override
        {
            Entries.push_back(std::move(entry));
        }

        std::vector<std::pair<std::string, std::string>> Writes;
        std::vector<CookedSourceEntry> Entries;
    };
} // namespace

TEST(ImportPrepare, LeavesActiveStateUntouched)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rock.src", "rock source bytes");

    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));

    LoggingProvider logging;
    PendingAssetImport pending;
    ASSERT_TRUE(PrepareAssetsOnDemand(root.Path(), importers, logging, pending));

    EXPECT_EQ(importer.ImportCount, 1);
    EXPECT_EQ(pending.Stats.Imported, 1u);
    ASSERT_EQ(pending.Artifacts.size(), 1u);
    ASSERT_EQ(pending.Registrations.size(), 1u);
    ASSERT_EQ(pending.IndexDelta.Puts.size(), 1u);

    // The cooked bytes exist in private staging, not at the active path, and no
    // active index was written.
    EXPECT_TRUE(std::filesystem::is_regular_file(pending.Artifacts[0].PreparedFile));
    EXPECT_FALSE(std::filesystem::exists(root.Path() / ".cooked/meshes/rock.smesh"));
    EXPECT_FALSE(std::filesystem::exists(root.Path() / ".cooked/index.json"));
}

TEST(ImportPrepare, AbandonedPendingCleansStagingAndLeavesActiveUntouched)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rock.src", "rock source bytes");

    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    LoggingProvider logging;

    std::filesystem::path staged;
    {
        PendingAssetImport pending;
        ASSERT_TRUE(PrepareAssetsOnDemand(root.Path(), importers, logging, pending));
        ASSERT_EQ(pending.Artifacts.size(), 1u);
        staged = pending.Artifacts[0].PreparedFile;
        ASSERT_TRUE(std::filesystem::is_regular_file(staged));
    }

    EXPECT_FALSE(std::filesystem::exists(staged));
    EXPECT_FALSE(std::filesystem::exists(root.Path() / ".cooked/meshes/rock.smesh"));
    EXPECT_FALSE(std::filesystem::exists(root.Path() / ".cooked/index.json"));
}

TEST(ImportPublish, RoutesBytesAndDeltaThroughPublisherOnly)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rock.src", "rock source bytes");

    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    LoggingProvider logging;

    PendingAssetImport pending;
    ASSERT_TRUE(PrepareAssetsOnDemand(root.Path(), importers, logging, pending));
    ASSERT_EQ(importer.ImportCount, 1);

    RecordingPublisher publisher;
    ASSERT_TRUE(PublishAssetImport(std::move(pending), publisher));

    // Publish runs no importer, and writes only through the publisher: the
    // active tree stays empty.
    EXPECT_EQ(importer.ImportCount, 1);
    ASSERT_EQ(publisher.Writes.size(), 1u);
    EXPECT_EQ(publisher.Writes[0].first, ".cooked/meshes/rock.smesh");
    EXPECT_EQ(publisher.Writes[0].second, "cooked:rock source bytes");
    ASSERT_EQ(publisher.Entries.size(), 1u);
    EXPECT_EQ(publisher.Entries[0].SourceRelPath, "meshes/rock.src");
    EXPECT_FALSE(std::filesystem::exists(root.Path() / ".cooked/meshes/rock.smesh"));
}

TEST(ImportPublish, RejectsDuplicateDestinationBeforeWriting)
{
    PendingAssetImport pending;
    CookedArtifact artifact{ "asset://meshes/dup.smesh", ".cooked/meshes/dup.smesh",
                             AssetType::StaticMesh, 1 };
    pending.Artifacts.push_back(PreparedCookedArtifact{ artifact, "/nonexistent/a" });
    pending.Artifacts.push_back(PreparedCookedArtifact{ artifact, "/nonexistent/b" });

    RecordingPublisher publisher;
    std::string error;
    EXPECT_FALSE(PublishAssetImport(std::move(pending), publisher, &error));
    EXPECT_TRUE(publisher.Writes.empty()) << "collision must be caught before any write";
    EXPECT_FALSE(error.empty());
}

TEST(ImportPublish, FilesystemWriterCommitsIndexLast)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rock.src", "rock source bytes");

    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    LoggingProvider logging;

    PendingAssetImport pending;
    ASSERT_TRUE(PrepareAssetsOnDemand(root.Path(), importers, logging, pending));

    FilesystemImportArtifactWriter writer(root.Path());
    ASSERT_TRUE(PublishAssetImport(std::move(pending), writer));

    // Bytes land in the active tree during publish; index membership only flips
    // when the owner saves, last.
    EXPECT_TRUE(std::filesystem::is_regular_file(root.Path() / ".cooked/meshes/rock.smesh"));
    EXPECT_FALSE(std::filesystem::exists(root.Path() / ".cooked/index.json"));

    ASSERT_TRUE(writer.Save());
    EXPECT_TRUE(std::filesystem::is_regular_file(root.Path() / ".cooked/index.json"));

    CookedCacheIndex index;
    ASSERT_TRUE(CookedCacheIndex::LoadFromFile(
        (root.Path() / ".cooked/index.json").generic_string(), index));
    EXPECT_NE(index.Find("meshes/rock.src"), nullptr);
}

TEST(ImportRegister, BindsProducedRecordsIntoRegistry)
{
    TempAssetRoot root;
    root.WriteFile("meshes/rock.src", "rock source bytes");

    FakeImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    LoggingProvider logging;

    PendingAssetImport pending;
    ASSERT_TRUE(PrepareAssetsOnDemand(root.Path(), importers, logging, pending));
    const std::vector<CookedArtifact> records = pending.Registrations;

    FilesystemImportArtifactWriter writer(root.Path());
    ASSERT_TRUE(PublishAssetImport(std::move(pending), writer));
    ASSERT_TRUE(writer.Save());

    AssetRegistry registry(logging);
    ASSERT_TRUE(RegisterImportedAssets(records, root.Path(), registry, logging));

    const AssetRecord* record = registry.FindByPath("asset://meshes/rock.smesh");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Type, AssetType::StaticMesh);
    EXPECT_TRUE(record->FilePath.ends_with(".cooked/meshes/rock.smesh"));
    EXPECT_NE(record->ContentHash, 0u);
}

#endif // SENCHA_ENABLE_COOK

TEST(ImportOnDemand, EditedImportSettingsSidecarRecooks)
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
    EXPECT_TRUE(importer.LastMeta.empty());

    // Writing the sidecar changes the freshness hash: same source recooks and
    // the importer receives the sidecar bytes.
    root.WriteFile("meshes/rock.src.meta", R"({"version": 1})");
    {
        AssetRegistry registry(logging);
        ImportOnDemandStats stats;
        ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging, &stats));
        EXPECT_EQ(stats.Imported, 1u);
    }
    EXPECT_EQ(importer.ImportCount, 2);
    EXPECT_EQ(importer.LastMeta, R"({"version": 1})");

    // Unchanged source + sidecar pair is fresh again.
    {
        AssetRegistry registry(logging);
        ImportOnDemandStats stats;
        ASSERT_TRUE(ImportAssetsOnDemand(root.PathString(), importers, registry, logging, &stats));
        EXPECT_EQ(stats.CookedFresh, 1u);
    }
    EXPECT_EQ(importer.ImportCount, 2);
}
