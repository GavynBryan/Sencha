#pragma once

#include <core/assets/AssetRegistry.h>

#include <cstddef>
#include <string_view>
#include <vector>

//=============================================================================
// IAssetSource
//
// The byte-source seam (docs/assets/pipeline.md, Decision I): loaders
// receive bytes, not paths. v1 has exactly one implementation — open file,
// read bytes — but the seam is what a pack-file reader plugs into later
// with no loader changes.
//
// Implementations must be pure with respect to engine state: ReadBytes is
// called from asset-loader work stages, which may run on task threads.
//=============================================================================
class IAssetSource
{
public:
    virtual ~IAssetSource() = default;

    // Reads the entire contents of `filePath` into `out`. Returns false on
    // failure (missing file, read error); `out` is left unspecified.
    [[nodiscard]] virtual bool ReadBytes(std::string_view filePath,
                                         std::vector<std::byte>& out) = 0;
};

class FileAssetSource final : public IAssetSource
{
public:
    [[nodiscard]] bool ReadBytes(std::string_view filePath,
                                 std::vector<std::byte>& out) override;
};

// Reads the bytes behind a record, resolving its physical location: records
// registered by the directory scanner carry a FilePath; procedural-era
// records may only have the virtual Path.
[[nodiscard]] bool ReadAssetBytes(IAssetSource& source,
                                  const AssetRecord& record,
                                  std::vector<std::byte>& out);
