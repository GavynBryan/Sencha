#pragma once

#include <assets/cook/CookedCache.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

//=============================================================================
// IAssetImporter (docs/assets/pipeline.md, Decision B)
//
// One per source format: the function that turns authored bytes (PNG, glTF,
// WAV, ...) into cooked runtime artifacts (.stex, .smesh, ...). Dev-only —
// compiled under SENCHA_ENABLE_COOK; in shipping builds only the cooked
// artifacts exist. These are exactly the functions a batch cook tool
// invokes, so `sencha-cook <assets-root>` falls out for free later.
//
// Contract, mirroring IAssetStager's stage half (Decision C):
//   - Import is pure with respect to engine state: bytes in, artifacts out
//     through the writer seam. No caches, no services, no logging — errors
//     travel in ImportResult::Error and the driver logs them.
//   - Every artifact's FileRelPath must live under .cooked/; the driver
//     rejects imports that write anywhere else.
//   - One source may produce many artifacts (Decision B keys the cooked
//     cache source → set of outputs from day one).
//=============================================================================

// Import-settings sidecar naming: "<source>.meta" beside the source file.
// Driver-level (any importer may have one); the schema belongs to the
// importer (textures: TextureImportSettings).
inline constexpr std::string_view kImportSettingsSuffix = ".meta";

// Where an importer reads sources its root file references: a document's
// stylesheets, a shader's includes. The mirror of ICookOutputWriter, and there
// for the same two reasons -- importers stay filesystem-free, and the driver
// sees every input a cook consumed.
//
// That second reason is the load-bearing one. Freshness is decided by hashing
// the inputs, so an input the driver never learned about is one that can change
// without recooking. Reading a sibling behind the driver's back does not just
// bypass a seam; it silently breaks the build cache.
class ISourceFileReader
{
public:
    virtual ~ISourceFileReader() = default;

    // Reads `relPath` (assets-root-relative, generic separators). False when it
    // does not exist or cannot be read -- which an importer should report as an
    // authoring error naming the file, not paper over.
    [[nodiscard]] virtual bool ReadSource(std::string_view relPath,
                                          std::vector<std::byte>& out) = 0;
};

struct ImportInput
{
    // Source file, relative to the assets root, generic separators.
    std::string_view SourceRelPath;

    // The source file's contents.
    std::span<const std::byte> Bytes;

    // Contents of the source's import-settings sidecar ("<source>.meta"),
    // empty when absent. The driver reads it (and folds it into the cooked
    // cache's freshness hash) so importers stay filesystem-free.
    std::span<const std::byte> MetaBytes{};

    // Additional sources the root file references, or null when the driver
    // supplies none (a caller cooking from memory). An importer that reads
    // through this MUST list what it read in ImportResult::AdditionalSources.
    ISourceFileReader* Sources = nullptr;
};

// Where importers write cooked artifacts. The seam keeps importers free of
// filesystem assumptions and lets tests run them against memory.
class ICookOutputWriter
{
public:
    virtual ~ICookOutputWriter() = default;

    // Writes `bytes` to `fileRelPath` (assets-root-relative), creating
    // directories as needed. Returns false on failure.
    [[nodiscard]] virtual bool WriteBytes(std::string_view fileRelPath,
                                          std::span<const std::byte> bytes) = 0;
};

// Destination for a prepared import: the byte sink plus the cooked index it
// belongs to, so a prepared import publishes artifact bytes and index entries
// through one owner. The owner (a standalone filesystem writer, or a document
// cook's transaction) commits its index as its own final act; PublishAssetImport
// never loads or saves index.json itself.
class IImportPublisher : public ICookOutputWriter
{
public:
    // Upsert one source entry into the index this publisher owns.
    virtual void PutIndexEntry(CookedSourceEntry entry) = 0;
};

struct ImportResult
{
    std::vector<CookedArtifact> Artifacts{};

    // Every additional source this import read through ImportInput::Sources,
    // assets-root-relative. The driver folds these into the freshness record,
    // so editing a shared stylesheet recooks every document that includes it.
    //
    // Listing one the import did not read only costs a stat. Omitting one it did
    // read is the bug this field exists to prevent, and it is invisible until
    // somebody edits that file and nothing happens.
    std::vector<std::string> AdditionalSources{};

    // Non-empty means the import failed. Importers report; the driver logs.
    std::string Error;

    [[nodiscard]] bool IsValid() const { return Error.empty(); }
};

class IAssetImporter
{
public:
    virtual ~IAssetImporter() = default;

    // Source extensions this importer handles, with the leading dot (".png").
    [[nodiscard]] virtual std::vector<std::string_view> SourceExtensions() const = 0;

    [[nodiscard]] virtual ImportResult Import(const ImportInput& input,
                                              ICookOutputWriter& output) = 0;
};

//=============================================================================
// AssetImporterRegistry
//
// Extension → importer. Non-owning: the cook driver's host owns the concrete
// importers (they are plain stateless objects). Owner-thread state, like
// every other asset registry — no locks, by the usual argument.
//=============================================================================
class AssetImporterRegistry
{
public:
    // Registers `importer` for each of its extensions. Returns false (and
    // registers nothing) if any extension is already claimed.
    bool Register(IAssetImporter& importer)
    {
        const std::vector<std::string_view> extensions = importer.SourceExtensions();
        for (const std::string_view extension : extensions)
        {
            if (ImportersByExtension.contains(std::string(extension)))
                return false;
        }
        for (const std::string_view extension : extensions)
            ImportersByExtension.emplace(std::string(extension), &importer);
        return true;
    }

    [[nodiscard]] IAssetImporter* FindByExtension(std::string_view extension) const
    {
        auto it = ImportersByExtension.find(std::string(extension));
        return it == ImportersByExtension.end() ? nullptr : it->second;
    }

    [[nodiscard]] bool Empty() const { return ImportersByExtension.empty(); }

private:
    std::unordered_map<std::string, IAssetImporter*> ImportersByExtension;
};
