#include <assets/cook/ImportOnDemand.h>

#include <assets/cook/CookedCache.h>
#include <core/assets/AssetRegistry.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    // Logger channel tag for the free-function driver.
    struct ImportOnDemandDriver
    {
    };

    constexpr std::string_view kIndexFileName = "index.json";

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

    // Registers the artifacts using their stored content hashes. An artifact
    // without one (a pre-hash index entry) is hashed from disk once and the
    // entry upgraded in place; `outHashesUpgraded` tells the caller to
    // re-save the index so the read never repeats.
    bool RegisterArtifacts(const std::filesystem::path& root,
                           std::vector<CookedArtifact>& artifacts,
                           AssetRegistry& registry,
                           Logger& log,
                           bool& outHashesUpgraded)
    {
        bool ok = true;
        for (CookedArtifact& artifact : artifacts)
        {
            AssetRecord record;
            record.Type = artifact.Type;
            record.SourceKind = AssetSourceKind::File;
            record.Path = artifact.Path;
            record.FilePath = (root / artifact.FileRelPath).generic_string();
            if (artifact.ContentHash == 0)
            {
                if (HashFileContents(record.FilePath, artifact.ContentHash))
                {
                    outHashesUpgraded = true;
                }
                else
                {
                    log.Warn("ImportOnDemand: could not hash cooked artifact '{}'", record.FilePath);
                    ok = false;
                }
            }
            record.ContentHash = artifact.ContentHash;
            ok = registry.RegisterOrVerify(record) && ok;
        }
        return ok;
    }
} // namespace

bool ImportAssetsOnDemand(std::string_view rootDirectory,
                          const AssetImporterRegistry& importers,
                          AssetRegistry& registry,
                          LoggingProvider& logging,
                          ImportOnDemandStats* outStats)
{
    Logger& log = logging.GetLogger<ImportOnDemandDriver>();

    ImportOnDemandStats stats;
    if (outStats)
        *outStats = stats;

    const std::filesystem::path root{ std::string(rootDirectory) };
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
    {
        log.Warn("ImportOnDemand: asset root '{}' is not a directory", root.generic_string());
        return false;
    }

    const std::filesystem::path cookedDir = root / kCookedCacheDirName;
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

    FileCookOutputWriter writer(root);
    bool ok = true;
    bool indexDirty = false;

    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec))
    {
        if (ec)
        {
            log.Warn("ImportOnDemand: scan skipped entry under '{}': {}",
                root.generic_string(), ec.message());
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
            std::filesystem::relative(it->path(), root).generic_string();

        std::filesystem::path metaPath = it->path();
        metaPath += std::string(kImportSettingsSuffix);
        const FileStat sourceStat = StatFile(it->path());
        const FileStat metaStat = StatFile(metaPath);

        const CookedSourceEntry* cached = index.Find(sourceRel);

        // Freshness fast path: unchanged size + mtime for the source and its
        // sidecar means the source bytes are never read. Any mismatch falls
        // through to the content hash, which stays the ground truth.
        if (cached != nullptr && sourceStat.MTime != 0
            && FileStat{ cached->SourceSize, cached->SourceMTime } == sourceStat
            && FileStat{ cached->MetaSize, cached->MetaMTime } == metaStat
            && ArtifactFilesExist(root, *cached))
        {
            ++stats.CookedFresh;
            CookedSourceEntry entry = *cached;
            bool hashesUpgraded = false;
            ok = RegisterArtifacts(root, entry.Artifacts, registry, log, hashesUpgraded) && ok;
            if (hashesUpgraded)
            {
                index.Put(std::move(entry));
                indexDirty = true;
            }
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

        if (cached != nullptr && cached->SourceHash == sourceHash
            && ArtifactFilesExist(root, *cached))
        {
            ++stats.CookedFresh;
            // Content unchanged under changed stats (a touch, a copy): stamp
            // the new stats so the next launch takes the fast path.
            CookedSourceEntry entry = *cached;
            StampSourceStats(entry, sourceStat, metaStat);
            bool hashesUpgraded = false;
            ok = RegisterArtifacts(root, entry.Artifacts, registry, log, hashesUpgraded) && ok;
            index.Put(std::move(entry));
            indexDirty = true;
            continue;
        }

        ImportResult result = importer->Import(ImportInput{ sourceRel, bytes, metaBytes }, writer);
        std::string whyNot = result.Error;
        if (!result.IsValid() || !ArtifactsAreValid(result.Artifacts, whyNot))
        {
            log.Warn("ImportOnDemand: import of '{}' failed: {}", sourceRel, whyNot);
            ++stats.Failed;
            ok = false;
            continue;
        }
        StampArtifactHashes(writer, result.Artifacts);

        CookedSourceEntry entry;
        entry.SourceRelPath = sourceRel;
        entry.SourceHash = sourceHash;
        StampSourceStats(entry, sourceStat, metaStat);
        entry.Artifacts = result.Artifacts;
        index.Put(std::move(entry));
        indexDirty = true;
        ++stats.Imported;
        log.Info("ImportOnDemand: cooked '{}' ({} artifact{})",
            sourceRel, result.Artifacts.size(), result.Artifacts.size() == 1 ? "" : "s");

        bool hashesUpgraded = false;
        ok = RegisterArtifacts(root, result.Artifacts, registry, log, hashesUpgraded) && ok;
    }

    if (indexDirty)
    {
        std::filesystem::create_directories(cookedDir, ec);
        if (ec || !index.SaveToFile(indexPath.generic_string()))
        {
            log.Warn("ImportOnDemand: could not write cooked index '{}'",
                indexPath.generic_string());
            ok = false;
        }
    }

    if (outStats)
        *outStats = stats;
    return ok;
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
    entry.SourceHash = sourceHash;
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
