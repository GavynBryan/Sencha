#pragma once

#include <span>
#include <string>
#include <vector>

class LoggingProvider;

//=============================================================================
// MaterialLibrary: the editor's list of pickable materials. (04-§3)
//
// Minimum-viable material browser: scans the project's content roots for
// `.smat` via the engine's ScanAssetsDirectory, so the editor and runtime
// agree on every asset:// path (no parallel editor asset-discovery path). The
// list is the seed of a general asset browser; for now it is material-scoped
// on purpose.
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

    // Rebuilds the list from a fresh scan of the given roots (the project's
    // content roots; the union matches what the asset system mounts). Missing
    // or empty roots contribute nothing.
    void Rescan(std::span<const std::string> roots);
    void Clear();

    [[nodiscard]] const std::vector<MaterialAsset>& Materials() const { return Entries; }
    [[nodiscard]] std::span<const std::string> Roots() const { return ScannedRoots; }

private:
    LoggingProvider& Logging;
    std::vector<MaterialAsset> Entries;
    std::vector<std::string> ScannedRoots;
};
