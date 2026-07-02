#pragma once

#include <string>
#include <string_view>
#include <vector>

class LoggingProvider;

//=============================================================================
// MaterialLibrary — the editor's list of pickable materials. (04-§3)
//
// Minimum-viable material browser: scans a project asset root for `.smat` via
// the engine's ScanAssetsDirectory, so the editor and runtime agree on every
// asset:// path (no parallel editor asset-discovery path). The list is the seed
// of a general asset browser; for now it is material-scoped on purpose.
//=============================================================================

struct MaterialAsset
{
    std::string Path;        // virtual path, e.g. asset://materials/dev/gray.smat
    std::string DisplayName; // friendly label, e.g. materials/dev/gray
};

class MaterialLibrary
{
public:
    explicit MaterialLibrary(LoggingProvider& logging);

    // Rebuilds the list from a fresh scan of the given root directory. A missing
    // or empty root clears the list (no fabricated entries).
    void Rescan(std::string_view rootDirectory);
    void Clear();

    [[nodiscard]] const std::vector<MaterialAsset>& Materials() const { return Entries; }
    [[nodiscard]] std::string_view Root() const { return ScannedRoot; }

private:
    LoggingProvider& Logging;
    std::vector<MaterialAsset> Entries;
    std::string ScannedRoot;
};
