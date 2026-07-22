#pragma once

#include "CookStepCache.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

// The active-publication paths one document cook reads and replaces, derived
// once from the source and the resolved document identity. A world cook publishes
// under a content-addressed stem inside its output namespace, so the stem folds
// the document hash; a standalone level cook publishes under its bare stem.
struct DocumentCookPaths
{
    std::string           Stem; // namespaced publication stem (no extension)
    std::filesystem::path CookedDir;
    std::filesystem::path Scene;
    std::filesystem::path Manifest;
    std::filesystem::path Collision;
    std::filesystem::path Receipt;
    std::filesystem::path Index;
};

[[nodiscard]] inline DocumentCookPaths DeriveDocumentCookPaths(
    const std::filesystem::path& assetsRoot, std::string_view sourceRel,
    std::string_view stem, const std::string& outputNamespace,
    std::uint64_t documentHash)
{
    DocumentCookPaths paths;
    paths.Stem = outputNamespace.empty()
        ? std::string(stem)
        : outputNamespace + "/" + std::format("{:016x}", documentHash);
    paths.CookedDir = assetsRoot / ".cooked/levels";
    paths.Scene = paths.CookedDir / (paths.Stem + ".cooked.json");
    paths.Manifest = paths.CookedDir / (paths.Stem + ".manifest.json");
    paths.Collision = paths.CookedDir / (paths.Stem + ".collision.json");
    paths.Receipt = DocumentCookReceiptPath(assetsRoot, sourceRel);
    paths.Index = assetsRoot / ".cooked/index.json";
    return paths;
}
