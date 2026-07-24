#include <assets/cook/ImportOnDemand.h>

#include <assets/cook/CookedCache.h>
#include <core/assets/AssetRegistry.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    // Logger channel tag for the free-function driver.
    struct ImportOnDemandDriver
    {
    };

    constexpr std::string_view kIndexFileName = "index.json";

    bool SetError(std::string* error, std::string message)
    {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    }

    class FileCookOutputWriter final : public ICookOutputWriter
    {
    public:
        explicit FileCookOutputWriter(std::filesystem::path root)
            : Root(std::move(root))
        {
        }

        bool WriteBytes(std::string_view fileRelPath, std::span<const std::byte> bytes) override
        {
            const std::filesystem::path full = Root / fileRelPath;
            std::error_code ec;
            std::filesystem::create_directories(full.parent_path(), ec);
            if (ec)
                return false;

            std::ofstream file(full, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
                return false;
            if (!bytes.empty())
                file.write(reinterpret_cast<const char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()));
            if (!file.good())
                return false;

            WrittenHashes[std::string(fileRelPath)] = HashBytes64(bytes);
            return true;
        }

        // Hash of the bytes written under `fileRelPath` this run; 0 if the
        // writer never wrote that path.
        [[nodiscard]] uint64_t WrittenHash(std::string_view fileRelPath) const
        {
            const auto it = WrittenHashes.find(std::string(fileRelPath));
            return it == WrittenHashes.end() ? 0 : it->second;
        }

    private:
        std::filesystem::path Root;
        std::unordered_map<std::string, uint64_t> WrittenHashes;
    };

    // Records the hash each artifact was written with, so registration and
    // future launches never have to read the artifact back.
    void StampArtifactHashes(const FileCookOutputWriter& writer,
                             std::vector<CookedArtifact>& artifacts)
    {
        for (CookedArtifact& artifact : artifacts)
            artifact.ContentHash = writer.WrittenHash(artifact.FileRelPath);
    }

    // Fills any zero content hash from the active artifact file (a pre-hash
    // cached entry), reporting whether anything changed so the caller can carry
    // the refreshed entry into the index delta. False if a file cannot be hashed.
    bool ResolveArtifactHashes(const std::filesystem::path& root,
                               std::vector<CookedArtifact>& artifacts,
                               Logger& log, bool& upgraded)
    {
        bool ok = true;
        for (CookedArtifact& artifact : artifacts)
        {
            if (artifact.ContentHash != 0)
                continue;
            const std::string file = (root / artifact.FileRelPath).generic_string();
            if (HashFileContents(file, artifact.ContentHash))
                upgraded = true;
            else
            {
                log.Warn("ImportOnDemand: could not hash cooked artifact '{}'", file);
                ok = false;
            }
        }
        return ok;
    }

    // Size + last-write time, the cheap freshness signal. {0, 0} doubles as
    // "absent or unreadable", which for the meta sidecar is a real state
    // (cooked without a sidecar) and must compare equal to itself.
    struct FileStat
    {
        uint64_t Size = 0;
        int64_t MTime = 0;

        bool operator==(const FileStat&) const = default;
    };

    FileStat StatFile(const std::filesystem::path& path)
    {
        std::error_code ec;
        const uint64_t size = std::filesystem::file_size(path, ec);
        if (ec)
            return {};
        const auto mtime = std::filesystem::last_write_time(path, ec);
        if (ec)
            return {};
        return FileStat{ size, static_cast<int64_t>(mtime.time_since_epoch().count()) };
    }

    void StampSourceStats(CookedSourceEntry& entry, const FileStat& source, const FileStat& meta)
    {
        entry.SourceSize = source.Size;
        entry.SourceMTime = source.MTime;
        entry.MetaSize = meta.Size;
        entry.MetaMTime = meta.MTime;
    }

    bool ReadFileBytes(const std::filesystem::path& path, std::vector<std::byte>& out)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;

        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size < 0)
            return false;
        file.seekg(0, std::ios::beg);

        out.resize(static_cast<std::size_t>(size));
        if (size > 0)
            file.read(reinterpret_cast<char*>(out.data()), size);
        return file.good() || size == 0;
    }

    // Source freshness = hash of the source bytes plus, when present, its
    // import-settings sidecar ("<source>.meta"): editing settings must recook.
    // An absent or empty sidecar leaves the plain source hash, so cooked
    // caches from before sidecars existed stay fresh.
    uint64_t HashSourceWithMeta(const std::filesystem::path& sourcePath,
                                std::span<const std::byte> sourceBytes,
                                std::vector<std::byte>& outMetaBytes)
    {
        outMetaBytes.clear();
        uint64_t hash = HashBytes64(sourceBytes);

        std::filesystem::path metaPath = sourcePath;
        metaPath += std::string(kImportSettingsSuffix);
        std::error_code ec;
        if (std::filesystem::exists(metaPath, ec) && ReadFileBytes(metaPath, outMetaBytes)
            && !outMetaBytes.empty())
        {
            const uint64_t metaHash = HashBytes64(std::span<const std::byte>(outMetaBytes));
            hash ^= metaHash + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        }
        return hash;
    }

    bool ArtifactsAreValid(const std::vector<CookedArtifact>& artifacts, std::string& whyNot)
    {
        if (artifacts.empty())
        {
            whyNot = "importer produced no artifacts";
            return false;
        }
        const std::string requiredPrefix = std::string(kCookedCacheDirName) + "/";
        for (const CookedArtifact& artifact : artifacts)
        {
            if (!artifact.FileRelPath.starts_with(requiredPrefix))
            {
                whyNot = "artifact '" + artifact.FileRelPath + "' is outside " + requiredPrefix;
                return false;
            }
            if (artifact.Type == AssetType::Unknown)
            {
                whyNot = "artifact '" + artifact.Path + "' has unknown asset type";
                return false;
            }
        }
        return true;
    }

    bool ArtifactFilesExist(const std::filesystem::path& root, const CookedSourceEntry& entry)
    {
        if (entry.Artifacts.empty())
            return false;
        for (const CookedArtifact& artifact : entry.Artifacts)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(root / artifact.FileRelPath, ec))
                return false;
        }
        return true;
    }

    // Private staging root for prepared bytes, unique per prepare run and under
    // .cooked so the source scan skips it.
    std::filesystem::path MakeStagingDir(const std::filesystem::path& root)
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        return root / kCookedCacheDirName / ".staging"
            / ("import_" + std::to_string(nonce));
    }
} // namespace

PendingAssetImport::PendingAssetImport(PendingAssetImport&& other) noexcept
    : Artifacts(std::move(other.Artifacts)),
      IndexDelta(std::move(other.IndexDelta)),
      Registrations(std::move(other.Registrations)),
      Stats(other.Stats),
      TempRoot(std::move(other.TempRoot))
{
    other.TempRoot.clear();
}

PendingAssetImport& PendingAssetImport::operator=(PendingAssetImport&& other) noexcept
{
    if (this != &other)
    {
        Cleanup();
        Artifacts = std::move(other.Artifacts);
        IndexDelta = std::move(other.IndexDelta);
        Registrations = std::move(other.Registrations);
        Stats = other.Stats;
        TempRoot = std::move(other.TempRoot);
        other.TempRoot.clear();
    }
    return *this;
}

PendingAssetImport::~PendingAssetImport()
{
    Cleanup();
}

void PendingAssetImport::Cleanup() noexcept
{
    if (TempRoot.empty())
        return;
    std::error_code ec;
    std::filesystem::remove_all(TempRoot, ec);
    TempRoot.clear();
}

bool PrepareAssetsOnDemand(const std::filesystem::path& assetsRoot,
                           const AssetImporterRegistry& importers,
                           LoggingProvider& logging,
                           PendingAssetImport& out)
{
    Logger& log = logging.GetLogger<ImportOnDemandDriver>();

    ImportOnDemandStats stats;
    std::error_code ec;
    if (!std::filesystem::is_directory(assetsRoot, ec))
    {
        log.Warn("ImportOnDemand: asset root '{}' is not a directory",
            assetsRoot.generic_string());
        return false;
    }

    const std::filesystem::path cookedDir = assetsRoot / kCookedCacheDirName;
    const std::filesystem::path indexPath = cookedDir / kIndexFileName;

    CookedCacheIndex index;
    if (std::filesystem::exists(indexPath, ec))
    {
        std::string indexError;
        if (!CookedCacheIndex::LoadFromFile(indexPath.generic_string(), index, &indexError))
        {
            // A corrupt index is a cold cache, not an error: everything recooks.
            log.Warn("ImportOnDemand: cooked index unreadable ({}); recooking all sources",
                indexError);
            index = {};
        }
    }

    // Imported bytes stage here and publish later; the pending owns and cleans
    // this directory. The writer creates it lazily, so an all-fresh run leaves
    // nothing behind.
    out.TempRoot = MakeStagingDir(assetsRoot);
    FileCookOutputWriter stagingWriter(out.TempRoot);

    bool ok = true;
    for (std::filesystem::recursive_directory_iterator it(assetsRoot, ec), end;
         it != end; it.increment(ec))
    {
        if (ec)
        {
            log.Warn("ImportOnDemand: scan skipped entry under '{}': {}",
                assetsRoot.generic_string(), ec.message());
            ok = false;
            ec.clear();
            continue;
        }

        if (!it->is_regular_file(ec))
        {
            if (it->path().filename() == kCookedCacheDirName)
                it.disable_recursion_pending();
            continue;
        }

        IAssetImporter* importer =
            importers.FindByExtension(it->path().extension().generic_string());
        if (importer == nullptr)
            continue;

        ++stats.SourcesSeen;
        const std::string sourceRel =
            std::filesystem::relative(it->path(), assetsRoot).generic_string();

        std::filesystem::path metaPath = it->path();
        metaPath += std::string(kImportSettingsSuffix);
        const FileStat sourceStat = StatFile(it->path());
        const FileStat metaStat = StatFile(metaPath);

        const CookedSourceEntry* cached = index.Find(sourceRel);

        // Freshness fast path: unchanged size + mtime for the source and its
        // sidecar means the source bytes are never read. Resolve any pre-hash
        // artifact hash so registration and the delta both carry it.
        if (cached != nullptr && sourceStat.MTime != 0
            && FileStat{ cached->SourceSize, cached->SourceMTime } == sourceStat
            && FileStat{ cached->MetaSize, cached->MetaMTime } == metaStat
            && ArtifactFilesExist(assetsRoot, *cached))
        {
            ++stats.CookedFresh;
            CookedSourceEntry entry = *cached;
            bool upgraded = false;
            ok = ResolveArtifactHashes(assetsRoot, entry.Artifacts, log, upgraded) && ok;
            out.Registrations.insert(out.Registrations.end(),
                entry.Artifacts.begin(), entry.Artifacts.end());
            if (upgraded)
                out.IndexDelta.Puts.push_back(std::move(entry));
            continue;
        }

        std::vector<std::byte> bytes;
        if (!ReadFileBytes(it->path(), bytes))
        {
            log.Warn("ImportOnDemand: could not read source '{}'", sourceRel);
            ++stats.Failed;
            ok = false;
            continue;
        }
        std::vector<std::byte> metaBytes;
        const uint64_t sourceHash = HashSourceWithMeta(it->path(), bytes, metaBytes);

        if (cached != nullptr && cached->InputFingerprint == sourceHash
            && ArtifactFilesExist(assetsRoot, *cached))
        {
            ++stats.CookedFresh;
            // Content unchanged under changed stats (a touch, a copy): stamp
            // the new stats so the next launch takes the fast path.
            CookedSourceEntry entry = *cached;
            StampSourceStats(entry, sourceStat, metaStat);
            bool upgraded = false;
            ok = ResolveArtifactHashes(assetsRoot, entry.Artifacts, log, upgraded) && ok;
            out.Registrations.insert(out.Registrations.end(),
                entry.Artifacts.begin(), entry.Artifacts.end());
            out.IndexDelta.Puts.push_back(std::move(entry));
            continue;
        }

        ImportResult result =
            importer->Import(ImportInput{ sourceRel, bytes, metaBytes }, stagingWriter);
        std::string whyNot = result.Error;
        if (!result.IsValid() || !ArtifactsAreValid(result.Artifacts, whyNot))
        {
            log.Warn("ImportOnDemand: import of '{}' failed: {}", sourceRel, whyNot);
            ++stats.Failed;
            ok = false;
            continue;
        }
        StampArtifactHashes(stagingWriter, result.Artifacts);

        CookedSourceEntry entry;
        entry.SourceRelPath = sourceRel;
        entry.InputFingerprint = sourceHash;
        StampSourceStats(entry, sourceStat, metaStat);
        entry.Artifacts = result.Artifacts;

        for (const CookedArtifact& artifact : result.Artifacts)
        {
            out.Artifacts.push_back(PreparedCookedArtifact{
                artifact, out.TempRoot / artifact.FileRelPath });
            out.Registrations.push_back(artifact);
        }
        out.IndexDelta.Puts.push_back(std::move(entry));
        ++stats.Imported;
        log.Info("ImportOnDemand: cooked '{}' ({} artifact{})",
            sourceRel, result.Artifacts.size(), result.Artifacts.size() == 1 ? "" : "s");
    }

    out.Stats = stats;
    return ok;
}

bool PublishAssetImport(PendingAssetImport pending,
                        IImportPublisher& publisher,
                        std::string* error)
{
    // Reject a destination claimed twice before writing anything: the caller
    // (or the transaction behind a composed publisher) would otherwise coalesce
    // two writers onto one file.
    std::unordered_set<std::string> destinations;
    for (const PreparedCookedArtifact& prepared : pending.Artifacts)
        if (!destinations.insert(prepared.Artifact.FileRelPath).second)
            return SetError(error, "duplicate cooked artifact destination '"
                + prepared.Artifact.FileRelPath + "'");

    for (const PreparedCookedArtifact& prepared : pending.Artifacts)
    {
        std::vector<std::byte> bytes;
        if (!ReadFileBytes(prepared.PreparedFile, bytes))
            return SetError(error, "could not read staged import '"
                + prepared.PreparedFile.generic_string() + "'");
        if (!publisher.WriteBytes(prepared.Artifact.FileRelPath, bytes))
            return SetError(error, "could not publish cooked artifact '"
                + prepared.Artifact.FileRelPath + "'");
    }

    for (CookedSourceEntry& entry : pending.IndexDelta.Puts)
        publisher.PutIndexEntry(std::move(entry));

    return true;
}

bool RegisterImportedAssets(std::span<const CookedArtifact> records,
                            const std::filesystem::path& assetsRoot,
                            AssetRegistry& registry,
                            LoggingProvider& logging)
{
    Logger& log = logging.GetLogger<ImportOnDemandDriver>();
    bool ok = true;
    for (const CookedArtifact& artifact : records)
    {
        AssetRecord record;
        record.Type = artifact.Type;
        record.SourceKind = AssetSourceKind::File;
        record.Path = artifact.Path;
        record.FilePath = (assetsRoot / artifact.FileRelPath).generic_string();
        record.ContentHash = artifact.ContentHash;
        // Preparation resolves hashes up front; a zero here means a file could
        // not be hashed then, so fall back to disk rather than register 0.
        if (record.ContentHash == 0 && !HashFileContents(record.FilePath, record.ContentHash))
            log.Warn("ImportOnDemand: could not hash cooked artifact '{}'", record.FilePath);
        ok = registry.RegisterOrVerify(record) && ok;
    }
    return ok;
}

FilesystemImportArtifactWriter::FilesystemImportArtifactWriter(std::filesystem::path assetsRoot)
    : Root(std::move(assetsRoot))
{
    const std::filesystem::path indexPath = Root / kCookedCacheDirName / kIndexFileName;
    std::error_code ec;
    if (std::filesystem::exists(indexPath, ec))
    {
        std::string indexError;
        if (!CookedCacheIndex::LoadFromFile(indexPath.generic_string(), Index, &indexError))
            Index = {}; // a corrupt index is a cold cache, preserved entries recook
    }
}

bool FilesystemImportArtifactWriter::WriteBytes(std::string_view fileRelPath,
                                                std::span<const std::byte> bytes)
{
    const std::filesystem::path full = Root / fileRelPath;
    std::error_code ec;
    std::filesystem::create_directories(full.parent_path(), ec);
    if (ec)
        return false;

    std::ofstream file(full, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    if (!bytes.empty())
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

void FilesystemImportArtifactWriter::PutIndexEntry(CookedSourceEntry entry)
{
    Index.Put(std::move(entry));
    Dirty = true;
}

bool FilesystemImportArtifactWriter::Save()
{
    if (!Dirty)
        return true;
    const std::filesystem::path indexPath = Root / kCookedCacheDirName / kIndexFileName;
    return Index.SaveToFile(indexPath.generic_string());
}

bool ImportAssetsOnDemand(std::string_view rootDirectory,
                          const AssetImporterRegistry& importers,
                          AssetRegistry& registry,
                          LoggingProvider& logging,
                          ImportOnDemandStats* outStats)
{
    Logger& log = logging.GetLogger<ImportOnDemandDriver>();
    const std::filesystem::path root{ std::string(rootDirectory) };

    PendingAssetImport pending;
    const bool prepared = PrepareAssetsOnDemand(root, importers, logging, pending);
    if (outStats != nullptr)
        *outStats = pending.Stats;

    // Publication consumes the pending by move, so copy the records to register
    // out first; a failed publish then registers nothing.
    const std::vector<CookedArtifact> registrations = pending.Registrations;

    FilesystemImportArtifactWriter writer(root);
    std::string publishError;
    const bool published = PublishAssetImport(std::move(pending), writer, &publishError);
    if (!published)
        log.Warn("ImportOnDemand: publish failed: {}", publishError);

    const bool saved = writer.Save();
    if (!saved)
        log.Warn("ImportOnDemand: could not write cooked index under '{}'", rootDirectory);

    const bool registered =
        published && RegisterImportedAssets(registrations, root, registry, logging);

    return prepared && published && saved && registered;
}

bool ReimportOneSource(std::string_view rootDirectory,
                       std::string_view sourceRelPath,
                       const AssetImporterRegistry& importers,
                       LoggingProvider& logging,
                       std::vector<std::string>& outArtifactPaths)
{
    Logger& log = logging.GetLogger<ImportOnDemandDriver>();
    outArtifactPaths.clear();

    const std::filesystem::path root{ std::string(rootDirectory) };
    const std::filesystem::path sourcePath = root / std::string(sourceRelPath);

    IAssetImporter* importer =
        importers.FindByExtension(sourcePath.extension().generic_string());
    if (importer == nullptr)
    {
        log.Warn("ReimportOneSource: no importer for '{}'", sourceRelPath);
        return false;
    }

    std::vector<std::byte> bytes;
    if (!ReadFileBytes(sourcePath, bytes))
    {
        log.Warn("ReimportOneSource: could not read source '{}'", sourceRelPath);
        return false;
    }
    std::vector<std::byte> metaBytes;
    const uint64_t sourceHash = HashSourceWithMeta(sourcePath, bytes, metaBytes);

    FileCookOutputWriter writer(root);
    ImportResult result = importer->Import(ImportInput{ sourceRelPath, bytes, metaBytes }, writer);
    std::string whyNot = result.Error;
    if (!result.IsValid() || !ArtifactsAreValid(result.Artifacts, whyNot))
    {
        log.Warn("ReimportOneSource: re-import of '{}' failed: {}", sourceRelPath, whyNot);
        return false;
    }
    StampArtifactHashes(writer, result.Artifacts);

    for (const CookedArtifact& artifact : result.Artifacts)
        outArtifactPaths.push_back(artifact.Path);

    // Update the cooked index on disk so a later cold start sees the source as
    // fresh and skips a redundant recook. Best-effort: a missing/corrupt index
    // just means the next launch recooks, which is correct, only slower.
    const std::filesystem::path cookedDir = root / kCookedCacheDirName;
    const std::filesystem::path indexPath = cookedDir / kIndexFileName;
    CookedCacheIndex index;
    std::error_code ec;
    if (std::filesystem::exists(indexPath, ec))
    {
        std::string indexError;
        if (!CookedCacheIndex::LoadFromFile(indexPath.generic_string(), index, &indexError))
            index = {};
    }
    CookedSourceEntry entry;
    entry.SourceRelPath = std::string(sourceRelPath);
    entry.InputFingerprint = sourceHash;
    std::filesystem::path metaPath = sourcePath;
    metaPath += std::string(kImportSettingsSuffix);
    StampSourceStats(entry, StatFile(sourcePath), StatFile(metaPath));
    entry.Artifacts = std::move(result.Artifacts);
    index.Put(std::move(entry));
    std::filesystem::create_directories(cookedDir, ec);
    if (!index.SaveToFile(indexPath.generic_string()))
        log.Warn("ReimportOneSource: could not update cooked index after reimporting '{}'", sourceRelPath);

    return true;
}
