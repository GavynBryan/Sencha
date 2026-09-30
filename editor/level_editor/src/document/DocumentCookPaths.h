#pragma once

#include "CookStepCache.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

// The active-publication paths one document cook reads and replaces, derived
// once from the source and the resolved document identity. The stem mirrors
// the source's directory under the cooked root (levels/room_2, prefabs/turret),
// so a document cooks beside its family wherever it was authored. A world cook
// publishes under a content-addressed stem inside its output namespace, so the
// stem folds the document hash; a standalone cook publishes under its bare stem.
struct DocumentCookPaths
{
    std::string           Stem; // directory-qualified publication stem (no extension)
    std::filesystem::path CookedDir;
    std::filesystem::path Scene; // the .smap, which carries dependencies and collision
    std::filesystem::path Receipt;
    std::filesystem::path Index;
};

[[nodiscard]] inline DocumentCookPaths DeriveDocumentCookPaths(
    const std::filesystem::path& assetsRoot, std::string_view sourceRel,
    std::string_view stem, const std::string& outputNamespace,
    std::uint64_t documentHash)
{
    DocumentCookPaths paths;
    const std::string sourceDir =
        std::filesystem::path(sourceRel).parent_path().generic_string();
    const std::string local = outputNamespace.empty()
        ? std::string(stem)
        : outputNamespace + "/" + std::format("{:016x}", documentHash);
    paths.Stem = sourceDir.empty() ? local : sourceDir + "/" + local;
    paths.CookedDir = assetsRoot / ".cooked";
    paths.Scene = paths.CookedDir / (paths.Stem + ".smap");
    paths.Receipt = DocumentCookReceiptPath(assetsRoot, sourceRel);
    paths.Index = assetsRoot / ".cooked/index.json";
    return paths;
}
