#pragma once

#include <assets/cook/CookedCache.h>
#include <core/json/JsonValue.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct CookStepReceipt
{
    std::string StepId;
    std::uint32_t Version = 1;
    std::uint64_t InputFingerprint = 0;
    std::vector<std::pair<std::string, std::uint64_t>> Dependencies;
    std::vector<CookedArtifact> Artifacts;
    JsonValue Metadata;
    double DurationMilliseconds = 0.0;
};

struct DocumentCookReceipt
{
    std::string Target;
    std::string PublishedProfileId;
    std::uint64_t PublishedFingerprint = 0;
    std::vector<std::string> PublishedOutputFamilies;
    std::vector<CookStepReceipt> Steps;
};

[[nodiscard]] const CookStepReceipt* FindCookStepReceipt(
    const DocumentCookReceipt& receipt, std::string_view stepId);
void PutCookStepReceipt(DocumentCookReceipt& receipt, CookStepReceipt step);

[[nodiscard]] JsonValue WriteDocumentCookReceipt(const DocumentCookReceipt& receipt);
[[nodiscard]] bool ReadDocumentCookReceipt(const JsonValue& value,
                                           DocumentCookReceipt& out,
                                           std::string* error = nullptr);
[[nodiscard]] bool LoadDocumentCookReceipt(const std::filesystem::path& path,
                                           DocumentCookReceipt& out,
                                           std::string* error = nullptr);
[[nodiscard]] bool SaveDocumentCookReceipt(const std::filesystem::path& path,
                                           const DocumentCookReceipt& receipt,
                                           std::string* error = nullptr);

