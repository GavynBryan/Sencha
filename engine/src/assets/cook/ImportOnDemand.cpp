#include <assets/cook/ImportOnDemand.h>

#include <assets/cook/CookedCache.h>
#include <core/assets/AssetRegistry.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>

#include <filesystem>
#include <fstream>
#include <string>
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
            return file.good();
        }

    private:
        std::filesystem::path Root;
    };

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

    bool RegisterArtifacts(const std::filesystem::path& root,
                           const std::vector<CookedArtifact>& artifacts,
                           AssetRegistry& registry,
                           Logger& log)
    {
        bool ok = true;
        for (const CookedArtifact& artifact : artifacts)
        {
            AssetRecord record;
            record.Type = artifact.Type;
            record.SourceKind = AssetSourceKind::File;
            record.Path = artifact.Path;
            record.FilePath = (root / artifact.FileRelPath).generic_string();
            if (!HashFileContents(record.FilePath, record.ContentHash))
            {
                log.Warn("ImportOnDemand: could not hash cooked artifact '{}'", record.FilePath);
                ok = false;
            }
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

        std::vector<std::byte> bytes;
        if (!ReadFileBytes(it->path(), bytes))
        {
            log.Warn("ImportOnDemand: could not read source '{}'", sourceRel);
            ++stats.Failed;
            ok = false;
            continue;
        }
        const uint64_t sourceHash = HashBytes64(bytes);

        if (const CookedSourceEntry* cached = index.Find(sourceRel);
            cached != nullptr && cached->SourceHash == sourceHash
            && ArtifactFilesExist(root, *cached))
        {
            ++stats.CookedFresh;
            ok = RegisterArtifacts(root, cached->Artifacts, registry, log) && ok;
            continue;
        }

        ImportResult result = importer->Import(ImportInput{ sourceRel, bytes }, writer);
        std::string whyNot = result.Error;
        if (!result.IsValid() || !ArtifactsAreValid(result.Artifacts, whyNot))
        {
            log.Warn("ImportOnDemand: import of '{}' failed: {}", sourceRel, whyNot);
            ++stats.Failed;
            ok = false;
            continue;
        }

        CookedSourceEntry entry;
        entry.SourceRelPath = sourceRel;
        entry.SourceHash = sourceHash;
        entry.Artifacts = result.Artifacts;
        index.Put(std::move(entry));
        indexDirty = true;
        ++stats.Imported;
        log.Info("ImportOnDemand: cooked '{}' ({} artifact{})",
            sourceRel, result.Artifacts.size(), result.Artifacts.size() == 1 ? "" : "s");

        ok = RegisterArtifacts(root, result.Artifacts, registry, log) && ok;
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
    const uint64_t sourceHash = HashBytes64(bytes);

    FileCookOutputWriter writer(root);
    ImportResult result = importer->Import(ImportInput{ sourceRelPath, bytes }, writer);
    std::string whyNot = result.Error;
    if (!result.IsValid() || !ArtifactsAreValid(result.Artifacts, whyNot))
    {
        log.Warn("ReimportOneSource: re-import of '{}' failed: {}", sourceRelPath, whyNot);
        return false;
    }

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
    entry.Artifacts = std::move(result.Artifacts);
    index.Put(std::move(entry));
    std::filesystem::create_directories(cookedDir, ec);
    if (!index.SaveToFile(indexPath.generic_string()))
        log.Warn("ReimportOneSource: could not update cooked index after reimporting '{}'", sourceRelPath);

    return true;
}
