#include "project/AssetAdoption.h"

#include "scene_source/Json5Parser.h"
#include "scene_source/Json5Writer.h"

#include <assets/cook/AssetImporter.h>
#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetIdentity.h>
#include <core/json/JsonFormat.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>

namespace
{
constexpr std::string_view kSceneSource = ".sscene";

// The id a file was born with, or the first free one after it.
AssetId FreeIdFor(std::string_view assetPath, const std::unordered_set<AssetId>& taken)
{
    AssetId id = AssetIdOfPath(assetPath);
    for (std::uint64_t salt = 1; taken.contains(id); ++salt)
        id = AssetIdOfPath(assetPath, salt);
    return id;
}

// `text` with the id it states removed, written back as its kind is written:
// a scene source as JSON5 (comments kept), everything else as JSON.
std::optional<std::string> Unstated(std::string_view text, bool json5)
{
    if (json5)
    {
        std::vector<std::string> endComments;
        std::optional<Json5Value> root = Json5Parse(text, nullptr, &endComments);
        if (!root.has_value() || !root->IsObject())
            return std::nullopt;
        std::erase_if(root->Members, [](const Json5Value::Member& member) { return member.first == kAssetIdKey; });
        return Json5Write(*root, endComments);
    }
    std::optional<JsonValue> root = JsonParse(text);
    if (!root.has_value() || !root->IsObject())
        return std::nullopt;
    ForgetStatedAssetId(*root);
    return JsonFormat(*root);
}

bool WriteText(const std::filesystem::path& file, const std::string& text)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    return out.good();
}
} // namespace

AssetAdoption PlanAdoption(std::span<const ProjectFile> files,
                           std::span<const std::pair<std::filesystem::path, AssetId>> orphans,
                           const std::unordered_set<std::string>& withSidecars)
{
    AssetAdoption plan;
    std::unordered_set<AssetId> taken;
    std::map<AssetId, std::vector<const ProjectFile*>> stating;
    for (const ProjectFile& file : files)
        if (file.StatedId.has_value())
        {
            stating[*file.StatedId].push_back(&file);
            taken.insert(*file.StatedId);
        }

    for (auto& [id, holders] : stating)
    {
        if (holders.size() < 2)
            continue;
        std::ranges::sort(holders, [&](const ProjectFile* a, const ProjectFile* b) {
            const bool aBorn = AssetIdOfPath(a->AssetPath) == id;
            const bool bBorn = AssetIdOfPath(b->AssetPath) == id;
            if (aBorn != bBorn)
                return aBorn;
            if (a->Modified != b->Modified)
                return a->Modified < b->Modified;
            return a->AssetPath < b->AssetPath;
        });
        for (std::size_t copy = 1; copy < holders.size(); ++copy)
        {
            const AssetId fresh = FreeIdFor(holders[copy]->AssetPath, taken);
            taken.insert(fresh);
            plan.Steps.push_back({ .What = AdoptionStep::Kind::Restate, .File = *holders[copy], .Id = fresh });
        }
    }

    // Files an orphaned sidecar returns to, and those of them it gives an id.
    std::unordered_set<std::string> claimed;
    std::unordered_set<std::string> statedBySidecar;
    for (const auto& [sidecar, id] : orphans)
    {
        const std::string name = std::filesystem::path(sidecar).replace_extension().filename().string();
        std::vector<const ProjectFile*> candidates;
        for (const ProjectFile& file : files)
            if (!file.StatedId.has_value() && !StatesIdInside(file.Extension) && !withSidecars.contains(file.AssetPath)
                && !claimed.contains(file.AssetPath) && file.FilePath.filename().string() == name)
                candidates.push_back(&file);
        if (candidates.size() != 1)
        {
            plan.Unpaired.push_back(sidecar);
            continue;
        }
        claimed.insert(candidates.front()->AssetPath);
        plan.Steps.push_back(
            { .What = AdoptionStep::Kind::Pair, .File = *candidates.front(), .Id = id, .Sidecar = sidecar });
        if (!id.IsValid())
            continue;
        statedBySidecar.insert(candidates.front()->AssetPath);
        if (!taken.insert(id).second)
        {
            // The sidecar names an id another file holds: it comes back as a copy.
            const AssetId fresh = FreeIdFor(candidates.front()->AssetPath, taken);
            taken.insert(fresh);
            plan.Steps.push_back({ .What = AdoptionStep::Kind::Restate, .File = *candidates.front(), .Id = fresh });
        }
    }

    for (const ProjectFile& file : files)
    {
        if (file.StatedId.has_value() || statedBySidecar.contains(file.AssetPath))
            continue;
        const AssetId id = FreeIdFor(file.AssetPath, taken);
        taken.insert(id);
        plan.Steps.push_back({ .What = AdoptionStep::Kind::State, .File = file, .Id = id });
    }
    return plan;
}

void ApplyAdoption(const AssetAdoption& adoption, std::vector<std::string>& errors)
{
    // Sidecars go back first: a file paired with one states its id through it.
    for (const AdoptionStep& step : adoption.Steps)
        if (step.What == AdoptionStep::Kind::Pair)
        {
            std::error_code ec;
            std::filesystem::rename(step.Sidecar, SidecarOf(step.File.FilePath), ec);
            if (ec)
                errors.push_back(step.Sidecar.generic_string() + ": " + ec.message());
        }

    for (const AdoptionStep& step : adoption.Steps)
    {
        if (step.What == AdoptionStep::Kind::Pair)
            continue;
        const std::filesystem::path statement = IdStatementOf(step.File.FilePath);
        std::optional<std::string> text = ReadProjectFileText(statement);
        if (step.What == AdoptionStep::Kind::Restate && text.has_value())
            text = Unstated(*text, step.File.Extension == kSceneSource);
        const std::optional<std::string> stated = StatingAssetId(text.value_or(std::string{}), step.Id);
        if (!stated.has_value() || !WriteText(statement, *stated))
            errors.push_back(statement.generic_string() + ": could not state its id");
    }
}

bool IsAssetFile(const ProjectFile& file, const AssetKindRegistry& kinds, const AssetImporterRegistry& importers)
{
    return StatesIdInside(file.Extension) || kinds.FindTypeByExtension(file.Extension) != AssetType::Unknown
        || importers.FindByExtension(file.Extension) != nullptr;
}

AssetAdoption AdoptAssetIds(std::span<const std::filesystem::path> roots,
                            const std::function<bool(const ProjectFile&)>& isAsset,
                            std::vector<std::string>& errors)
{
    std::vector<ProjectFile> files = ScanProjectFiles(roots);
    std::erase_if(files, [&](const ProjectFile& file) { return !isAsset(file); });
    std::unordered_set<std::string> withSidecars;
    for (const ProjectFile& file : files)
    {
        std::error_code ec;
        if (std::filesystem::exists(SidecarOf(file.FilePath), ec))
            withSidecars.insert(file.AssetPath);
    }

    std::vector<std::pair<std::filesystem::path, AssetId>> orphans;
    for (const std::filesystem::path& sidecar : ScanOrphanedSidecars(roots))
    {
        const std::optional<std::string> text = ReadProjectFileText(sidecar);
        orphans.emplace_back(sidecar, text.has_value() ? StatedAssetIdIn(*text).value_or(AssetId{}) : AssetId{});
    }

    AssetAdoption plan = PlanAdoption(files, orphans, withSidecars);
    ApplyAdoption(plan, errors);
    return plan;
}
