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
// Contract, mirroring IAssetLoader's stage half (Decision C):
//   - Import is pure with respect to engine state: bytes in, artifacts out
//     through the writer seam. No caches, no services, no logging — errors
//     travel in ImportResult::Error and the driver logs them.
//   - Every artifact's FileRelPath must live under .cooked/; the driver
//     rejects imports that write anywhere else.
//   - One source may produce many artifacts (Decision B keys the cooked
//     cache source → set of outputs from day one).
//=============================================================================

struct ImportInput
{
    // Source file, relative to the assets root, generic separators.
    std::string_view SourceRelPath;

    // The source file's contents.
    std::span<const std::byte> Bytes;
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

struct ImportResult
{
    std::vector<CookedArtifact> Artifacts;

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
