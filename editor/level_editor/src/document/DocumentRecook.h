#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

class LoggingProvider;
struct RuntimeAssets;

// Recooks every document cooked under `assetsRoot` that composed from one of
// `savedAssetPaths` (asset://...sscene), as the cook index recorded it. Returns
// their source paths, assets-root-relative, in path order. Owner thread: a cook
// collects its input through the asset system.
[[nodiscard]] std::vector<std::string> RecookComposedDependents(std::span<const std::string> savedAssetPaths,
                                                                const std::filesystem::path& assetsRoot,
                                                                double cellSize,
                                                                LoggingProvider& logging,
                                                                RuntimeAssets* assets = nullptr);
