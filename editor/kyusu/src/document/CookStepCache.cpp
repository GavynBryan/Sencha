#include "CookStepCache.h"

#include "CookArtifactTransaction.h"
#include "CookGraph.h"

#include <core/hash/ContentHash.h>

#include <format>

namespace
{
    bool ReceiptArtifactsMatch(const std::filesystem::path& assetsRoot,
                               const CookStepReceipt& receipt)
    {
        for (const CookedArtifact& artifact : receipt.Artifacts)
        {
            const std::filesystem::path path = assetsRoot / artifact.FileRelPath;
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
                return false;
            std::uint64_t actual = 0;
            if (!HashFileContents(path.generic_string(), actual)
                || actual != artifact.ContentHash)
                return false;
        }
        return true;
    }

    bool CopyFile(const std::filesystem::path& source,
                  const std::filesystem::path& destination,
                  std::string* error)
    {
        std::error_code ec;
        std::filesystem::create_directories(destination.parent_path(), ec);
        std::filesystem::copy_file(source, destination,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec)
            return true;
        if (error != nullptr)
            *error = "could not copy cook artifact '" + source.generic_string()
                + "' to '" + destination.generic_string() + "'";
        return false;
    }
}

std::filesystem::path DocumentCookReceiptPath(
    const std::filesystem::path& assetsRoot, std::string_view sourceRel)
{
    return assetsRoot / ".cooked/.cook-state"
        / std::format("{:016x}.json", HashBytes64(sourceRel));
}

bool CookedSourceArtifactsMatch(const std::filesystem::path& assetsRoot,
                                const CookedSourceEntry& entry)
{
    for (const CookedArtifact& artifact : entry.Artifacts)
    {
        const std::filesystem::path path = assetsRoot / artifact.FileRelPath;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return false;
        if (artifact.ContentHash != 0)
        {
            std::uint64_t actual = 0;
            if (!HashFileContents(path.generic_string(), actual)
                || actual != artifact.ContentHash)
                return false;
        }
    }
    return true;
}

const CookStepReceipt* FindReusableCookStep(
    const DocumentCookReceipt* receipt,
    const std::filesystem::path& assetsRoot,
    std::string_view stepId,
    std::uint64_t fingerprint)
{
    if (receipt == nullptr)
        return nullptr;
    const CookStepReceipt* step = FindCookStepReceipt(*receipt, stepId);
    const CookStepDefinition* definition = FindDocumentCookStep(stepId);
    if (step == nullptr || definition == nullptr
        || step->Version != definition->Version
        || step->InputFingerprint != fingerprint
        || !ReceiptArtifactsMatch(assetsRoot, *step))
        return nullptr;
    return step;
}

bool RestoreCookStepArtifacts(
    const CookStepReceipt& receipt,
    const std::filesystem::path& assetsRoot,
    CookArtifactTransaction& transaction,
    std::vector<CookedArtifact>& publicArtifacts,
    std::unordered_set<std::string>& generatedPaths,
    std::vector<std::string>& materialRefs,
    bool sceneReference,
    std::string* error)
{
    constexpr std::string_view prefix = "asset://";
    for (const CookedArtifact& cached : receipt.Artifacts)
    {
        if (!cached.Path.starts_with(prefix))
        {
            if (error != nullptr)
                *error = "cached step artifact has an invalid asset path";
            return false;
        }
        const std::string relative(cached.Path.substr(prefix.size()));
        const std::filesystem::path active = assetsRoot / ".cooked" / relative;
        if (!CopyFile(assetsRoot / cached.FileRelPath,
                      transaction.Stage(active), error))
            return false;
        publicArtifacts.push_back(CookedArtifact{
            cached.Path, ".cooked/" + relative, cached.Type, cached.ContentHash });
        if (sceneReference)
        {
            generatedPaths.insert(cached.Path);
            materialRefs.push_back(cached.Path);
        }
    }
    return true;
}

CookedArtifact CacheCookStepArtifact(
    const CookedArtifact& artifact,
    std::string_view sourceRel,
    std::string_view stepId,
    std::uint64_t fingerprint,
    const std::filesystem::path& assetsRoot,
    CookArtifactTransaction& transaction,
    std::string* error)
{
    const std::filesystem::path publicStaged =
        transaction.Stage(assetsRoot / artifact.FileRelPath);
    const std::string privateRel = std::format(
        ".cooked/.cook-state/{:016x}/{}/{:016x}/{}",
        HashBytes64(sourceRel), stepId, fingerprint,
        std::filesystem::path(artifact.FileRelPath).filename().generic_string());
    const std::filesystem::path privateActive = assetsRoot / privateRel;
    if (!CopyFile(publicStaged, transaction.Stage(privateActive), error))
        return {};
    std::uint64_t hash = 0;
    if (!HashFileContents(publicStaged.generic_string(), hash))
    {
        if (error != nullptr)
            *error = "could not fingerprint staged cook artifact '"
                + publicStaged.generic_string() + "'";
        return {};
    }
    return CookedArtifact{ artifact.Path, privateRel, artifact.Type, hash };
}

void RestoreDocumentCookResultMetadata(
    const JsonValue& metadata, DocumentCookResult& result)
{
    const auto number = [&metadata](const char* field) -> double
    {
        const JsonValue* value = metadata.Find(field);
        return value != nullptr && value->IsNumber() ? value->AsNumber() : 0.0;
    };
    result.CellCount = static_cast<std::size_t>(number("cell_count"));
    result.DirectLightCount = static_cast<std::size_t>(number("direct_light_count"));
    result.ProbeVolumeCount = static_cast<std::size_t>(number("probe_volume_count"));
    result.ProbeCount = static_cast<std::size_t>(number("probe_count"));
    result.LightmapAtlasWidth = static_cast<std::uint32_t>(number("atlas_width"));
    result.LightmapAtlasHeight = static_cast<std::uint32_t>(number("atlas_height"));
    result.EffectiveLuxelSize = static_cast<float>(number("effective_luxel_size"));
}

JsonValue DocumentCookResultMetadata(const DocumentCookResult& result)
{
    return JsonValue(JsonValue::Object{
        { "cell_count", JsonValue(static_cast<double>(result.CellCount)) },
        { "direct_light_count", JsonValue(static_cast<double>(result.DirectLightCount)) },
        { "probe_volume_count", JsonValue(static_cast<double>(result.ProbeVolumeCount)) },
        { "probe_count", JsonValue(static_cast<double>(result.ProbeCount)) },
        { "atlas_width", JsonValue(static_cast<double>(result.LightmapAtlasWidth)) },
        { "atlas_height", JsonValue(static_cast<double>(result.LightmapAtlasHeight)) },
        { "effective_luxel_size", JsonValue(result.EffectiveLuxelSize) },
    });
}
