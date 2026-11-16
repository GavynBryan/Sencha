#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

class LoggingProvider;
struct RuntimeAssets;

// Owner thread: a cook collects its input through the asset system.
[[nodiscard]] std::vector<std::string> RecookComposedDependents(std::span<const std::string> savedAssetPaths,
                                                                const std::filesystem::path& assetsRoot,
                                                                double cellSize,
                                                                LoggingProvider& logging,
                                                                RuntimeAssets* assets = nullptr);
