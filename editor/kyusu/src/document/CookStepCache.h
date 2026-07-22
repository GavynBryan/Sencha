#pragma once

#include "CookReceipt.h"
#include "DocumentCook.h"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

class CookArtifactTransaction;

[[nodiscard]] std::filesystem::path DocumentCookReceiptPath(
    const std::filesystem::path& assetsRoot, std::string_view sourceRel);
[[nodiscard]] bool CookedSourceArtifactsMatch(
    const std::filesystem::path& assetsRoot, const CookedSourceEntry& entry);
[[nodiscard]] const CookStepReceipt* FindReusableCookStep(
    const DocumentCookReceipt* receipt,
    const std::filesystem::path& assetsRoot,
    std::string_view stepId,
    std::uint64_t fingerprint);
[[nodiscard]] bool RestoreCookStepArtifacts(
    const CookStepReceipt& receipt,
    const std::filesystem::path& assetsRoot,
    CookArtifactTransaction& transaction,
    std::vector<CookedArtifact>& publicArtifacts,
    std::unordered_set<std::string>& generatedPaths,
    std::vector<std::string>& materialRefs,
    bool sceneReference,
    std::string* error = nullptr);
[[nodiscard]] CookedArtifact CacheCookStepArtifact(
    const CookedArtifact& artifact,
    std::string_view sourceRel,
    std::string_view stepId,
    std::uint64_t fingerprint,
    const std::filesystem::path& assetsRoot,
    CookArtifactTransaction& transaction,
    std::string* error = nullptr);

void RestoreDocumentCookResultMetadata(
    const JsonValue& metadata, DocumentCookResult& result);
[[nodiscard]] JsonValue DocumentCookResultMetadata(
    const DocumentCookResult& result);
